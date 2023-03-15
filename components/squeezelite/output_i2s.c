/* 
 *  Squeezelite for esp32
 *
 *  (c) Sebastien 2019
 *      Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
 
/* 
Synchronisation is a bit of a hack with i2s. The esp32 driver is always
full when it starts, so there is a delay of the total length of buffers.
In other words, i2s_write blocks at first call, until at least one buffer
has been written (it uses a queue with produce / consume).

The first hack is to consume that length at the beginning of tracks when
synchronization is active. It's about ~180ms @ 44.1kHz

The second hack is that we never know exactly the number of frames in the 
DMA buffers when we update the output.frames_played_dmp. We assume that
after i2s_write, these buffers are always full so by measuring the gap
between time after i2s_write and update of frames_played_dmp, we have a
good idea of the error. 

The third hack is when sample rate changes, buffers are reset and we also
do the change too early, but can't do that exaclty at the right time. So 
there might be a pop and a de-sync when sampling rate change happens. Not
sure that using rate_delay would fix that
*/

#include "squeezelite.h"
#include "slimproto.h"
#include "esp_pthread.h"
#include "driver/i2s.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "perf_trace.h"
#include <signal.h>
#include "adac.h"
#include "time.h"
#include "led.h"
#include "monitor.h"
#include "platform_config.h"
#include "gpio_exp.h"
#include "accessors.h"
#include "equalizer.h"
#include "globdefs.h"

#define LOCK   mutex_lock(outputbuf->mutex)
#define UNLOCK mutex_unlock(outputbuf->mutex)

#define FRAME_BLOCK MAX_SILENCE_FRAMES
#define SPDIF_BLOCK	256

// must have an integer ratio with FRAME_BLOCK (see spdif comment)
#define DMA_BUF_LEN		512	
#define DMA_BUF_COUNT	12

#define DECLARE_ALL_MIN_MAX 	\
	DECLARE_MIN_MAX(o); 		\
	DECLARE_MIN_MAX(s); 		\
	DECLARE_MIN_MAX(rec); 		\
	DECLARE_MIN_MAX(i2s_time); 	\
	DECLARE_MIN_MAX(buffering);

#define RESET_ALL_MIN_MAX 		\
	RESET_MIN_MAX(o); 			\
	RESET_MIN_MAX(s); 			\
	RESET_MIN_MAX(rec);	\
	RESET_MIN_MAX(i2s_time);	\
	RESET_MIN_MAX(buffering);
	
#define STATS_PERIOD_MS 5000
#define STAT_STACK_SIZE	(3*1024)

#ifndef CONFIG_AMP_GPIO_LEVEL
#define CONFIG_AMP_GPIO_LEVEL 1
#endif

extern struct outputstate output;
extern struct buffer *streambuf;
extern struct buffer *outputbuf;
extern u8_t *silencebuf;

const struct adac_s *dac_set[] = { &dac_tas57xx, &dac_tas5713, &dac_ac101, &dac_wm8978, &dac_wm8711bl, NULL };
const struct adac_s *adac = &dac_external;

static log_level loglevel;

static bool (*slimp_handler_chain)(u8_t *data, int len);
static bool jack_mutes_amp;
static bool running, isI2SStarted, ended;
static i2s_config_t i2s_config;
static u8_t *obuf;
static frames_t oframes;
static struct {
	bool enabled;
	u8_t *buf;
	size_t count;
} spdif;
static size_t dma_buf_frames;
static TaskHandle_t stats_task, output_i2s_task;
static bool stats;
static struct {
	int gpio, active;
} amp_control = { CONFIG_AMP_GPIO, CONFIG_AMP_GPIO_LEVEL },
  mute_control = { CONFIG_MUTE_GPIO, CONFIG_MUTE_GPIO_LEVEL };

DECLARE_ALL_MIN_MAX;

static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, u8_t flags,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr);
static void output_thread_i2s(void *arg);
static void output_thread_i2s_stats(void *arg);
static void spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst, size_t *count);
static void (*jack_handler_chain)(bool inserted);

#define I2C_PORT	0

/****************************************************************************************
 * AUDO packet handler
 */
static bool handler(u8_t *data, int len){
	bool res = true;
	
	if (!strncmp((char*) data, "audo", 4)) {
		struct audo_packet *pkt = (struct audo_packet*) data;
		// 0 = headphone (internal speakers off), 1 = sub out,
		// 2 = always on (internal speakers on), 3 = always off	

		if (jack_mutes_amp != (pkt->config == 0)) {
			jack_mutes_amp = pkt->config == 0;
			config_set_value(NVS_TYPE_STR, "jack_mutes_amp", jack_mutes_amp ? "y" : "n");		
			
			if (jack_mutes_amp && jack_inserted_svc()) {
				adac->speaker(false);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, !amp_control.active);
			} else {
				adac->speaker(true);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, amp_control.active);
			}	
		}

		LOG_INFO("got AUDO %02x", pkt->config);
	} else {
		res = false;
	}
	
	// chain protocol handlers (bitwise or is fine)
	if (*slimp_handler_chain) res |= (*slimp_handler_chain)(data, len);
	
	return res;
}

/****************************************************************************************
 * jack insertion handler
 */
static void jack_handler(bool inserted) {
	// jack detection bounces a bit but that seems fine
	if (jack_mutes_amp) {
		LOG_INFO("switching amplifier %s", inserted ? "OFF" : "ON");
		adac->speaker(!inserted);
		if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, inserted ? !amp_control.active : amp_control.active);
	}
	
	// activate headset
	adac->headset(inserted);
	
	// and chain if any
	if (jack_handler_chain) (jack_handler_chain)(inserted);
}

/****************************************************************************************
 * amp GPIO
 */
#ifndef AMP_LOCKED 
static void set_amp_gpio(int gpio, char *value) {
	char *p;
	
	if (strcasestr(value, "amp")) {
		amp_control.gpio = gpio;
		if ((p = strchr(value, ':')) != NULL) amp_control.active = atoi(p + 1);
	}	
}	
#endif

/****************************************************************************************
 * Set pin from config string
 */
static void set_i2s_pin(char *config, i2s_pin_config_t *pin_config) {
	pin_config->bck_io_num = pin_config->ws_io_num = pin_config->data_out_num = pin_config->data_in_num = -1; 				
	PARSE_PARAM(config, "bck", '=', pin_config->bck_io_num);
	PARSE_PARAM(config, "ws", '=', pin_config->ws_io_num);
	PARSE_PARAM(config, "do", '=', pin_config->data_out_num);
}

/****************************************************************************************
 * Initialize the DAC output
 */
void output_init_i2s(log_level level, char *device, unsigned output_buf_size, char *params, unsigned rates[], unsigned rate_delay, unsigned idle) {
	loglevel = level;
	int silent_do = -1;
	char *p;
	esp_err_t res;

	// chain SLIMP handlers
	slimp_handler_chain = slimp_handler;
	slimp_handler = handler;	
	
	p = config_alloc_get_default(NVS_TYPE_STR, "jack_mutes_amp", "n", 0);
	jack_mutes_amp = (strcmp(p,"1") == 0 ||strcasecmp(p,"y") == 0);
	free(p);
	
#if BYTES_PER_FRAME == 8
	output.format = S32_LE;
#else
	output.format = S16_LE;
#endif

	output.write_cb = &_i2s_write_frames;
	
	obuf = malloc(FRAME_BLOCK * BYTES_PER_FRAME);
	if (!obuf) {
		LOG_ERROR("Cannot allocate i2s buffer");
		return;
	}
		
	running = true;

	// get SPDIF configuration from NVS or compile
	char *spdif_config = config_alloc_get_str("spdif_config", CONFIG_SPDIF_CONFIG, "bck=" STR(CONFIG_SPDIF_BCK_IO) 
											  ",ws=" STR(CONFIG_SPDIF_WS_IO) ",do=" STR(CONFIG_SPDIF_DO_IO));
											  
	char *dac_config = config_alloc_get_str("dac_config", CONFIG_DAC_CONFIG, "model=i2s,bck=" STR(CONFIG_I2S_BCK_IO) 
											",ws=" STR(CONFIG_I2S_WS_IO) ",do=" STR(CONFIG_I2S_DO_IO) 
											",sda=" STR(CONFIG_I2C_SDA) ",scl=" STR(CONFIG_I2C_SCL)
											",mute=" STR(CONFIG_MUTE_GPIO));	

	i2s_pin_config_t i2s_dac_pin, i2s_spdif_pin;											
	set_i2s_pin(spdif_config, &i2s_spdif_pin);										
	set_i2s_pin(dac_config, &i2s_dac_pin);										

	/* BEWARE: i2s.c must be patched otherwise L/R are swapped in 32 bits mode */
	 
	// common I2S initialization
	i2s_config.mode = I2S_MODE_MASTER | I2S_MODE_TX;
	i2s_config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
	i2s_config.communication_format = I2S_COMM_FORMAT_I2S| I2S_COMM_FORMAT_I2S_MSB;
	// in case of overflow, do not replay old buffer
	i2s_config.tx_desc_auto_clear = true;		
	i2s_config.use_apll = true;
	i2s_config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1; //Interrupt level 1
	
	if (strcasestr(device, "spdif")) {
		spdif.enabled = true;	
		if ((spdif.buf = heap_caps_malloc(SPDIF_BLOCK * 16, MALLOC_CAP_INTERNAL)) == NULL) {
			LOG_ERROR("Cannot allocate SPDIF buffer");
		}
	
		if (i2s_spdif_pin.bck_io_num == -1 || i2s_spdif_pin.ws_io_num == -1 || i2s_spdif_pin.data_out_num == -1) {
			LOG_WARN("Cannot initialize I2S for SPDIF bck:%d ws:%d do:%d", i2s_spdif_pin.bck_io_num, 
																		   i2s_spdif_pin.ws_io_num, 
																		   i2s_spdif_pin.data_out_num);
		}
									
		i2s_config.sample_rate = output.current_sample_rate * 2;
		i2s_config.bits_per_sample = 32;
		// Normally counted in frames, but 16 sample are transformed into 32 bits in spdif
		i2s_config.dma_buf_len = DMA_BUF_LEN / 2;	
		i2s_config.dma_buf_count = DMA_BUF_COUNT * 2;
		/* 
		   In DMA, we have room for (LEN * COUNT) frames of 32 bits samples that 
		   we push at sample_rate * 2. Each of these peuso-frames is a single true
		   audio frame. So the real depth is true frames is (LEN * COUNT / 2)
		*/   
		dma_buf_frames = DMA_BUF_COUNT * DMA_BUF_LEN / 2;	
		
		// silence DAC output if sharing the same ws/bck
		if (i2s_dac_pin.ws_io_num == i2s_spdif_pin.ws_io_num && i2s_dac_pin.bck_io_num == i2s_spdif_pin.bck_io_num)	silent_do = i2s_dac_pin.data_out_num;		
		
		res = i2s_driver_install(CONFIG_I2S_NUM, &i2s_config, 0, NULL);
		res |= i2s_set_pin(CONFIG_I2S_NUM, &i2s_spdif_pin);
		LOG_INFO("SPDIF using I2S bck:%d, ws:%d, do:%d", i2s_spdif_pin.bck_io_num, i2s_spdif_pin.ws_io_num, i2s_spdif_pin.data_out_num);
	} else {
		i2s_config.sample_rate = output.current_sample_rate;
		i2s_config.bits_per_sample = BYTES_PER_FRAME * 8 / 2;
		// Counted in frames (but i2s allocates a buffer <= 4092 bytes)
		i2s_config.dma_buf_len = DMA_BUF_LEN;	
		i2s_config.dma_buf_count = DMA_BUF_COUNT;
		dma_buf_frames = DMA_BUF_COUNT * DMA_BUF_LEN;	
		
		// silence SPDIF output
		silent_do = i2s_spdif_pin.data_out_num;		

		char model[32] = "i2s";
		if ((p = strcasestr(dac_config, "model")) != NULL) sscanf(p, "%*[^=]=%31[^,]", model);
		if ((p = strcasestr(dac_config, "mute")) != NULL) {
			char mute[8] = "";
			sscanf(p, "%*[^=]=%7[^,]", mute);
			mute_control.gpio = atoi(mute);
			if ((p = strchr(mute, ':')) != NULL) mute_control.active = atoi(p + 1);
		}	

		for (int i = 0; adac == &dac_external && dac_set[i]; i++) if (strcasestr(dac_set[i]->model, model)) adac = dac_set[i];
		res = adac->init(dac_config, I2C_PORT, &i2s_config) ? ESP_OK : ESP_FAIL;

		res |= i2s_driver_install(CONFIG_I2S_NUM, &i2s_config, 0, NULL);
		res |= i2s_set_pin(CONFIG_I2S_NUM, &i2s_dac_pin);
		
		if (res == ESP_OK && mute_control.gpio >= 0) {
			gpio_pad_select_gpio(mute_control.gpio);
			gpio_set_direction(mute_control.gpio, GPIO_MODE_OUTPUT);
			gpio_set_level(mute_control.gpio, mute_control.active);
		}		
				
		LOG_INFO("%s DAC using I2S bck:%d, ws:%d, do:%d, mute:%d:%d (res:%d)", model, i2s_dac_pin.bck_io_num, i2s_dac_pin.ws_io_num, 
																   i2s_dac_pin.data_out_num, mute_control.gpio, mute_control.active, res);
	}	
			
	free(dac_config);
	free(spdif_config);
	
	if (res != ESP_OK) {
		LOG_WARN("no DAC configured");
		return;
	}	
	
	// turn off GPIO than is not used (SPDIF of DAC DO when shared)
	if (silent_do >= 0) {
		gpio_pad_select_gpio(silent_do);
		gpio_set_direction(silent_do, GPIO_MODE_OUTPUT);
		gpio_set_level(silent_do, 0);
	}	

	LOG_INFO("Initializing I2S mode %s with rate: %d, bits per sample: %d, buffer frames: %d, number of buffers: %d ", 
			spdif.enabled ? "S/PDIF" : "normal", 
			i2s_config.sample_rate, i2s_config.bits_per_sample, i2s_config.dma_buf_len, i2s_config.dma_buf_count);
	
	i2s_stop(CONFIG_I2S_NUM);
	i2s_zero_dma_buffer(CONFIG_I2S_NUM);
	isI2SStarted=false;
	
	adac->power(ADAC_STANDBY);

	jack_handler_chain = jack_handler_svc;
	jack_handler_svc = jack_handler;
	
#ifndef AMP_LOCKED	
	parse_set_GPIO(set_amp_gpio);
#endif

	if (amp_control.gpio != -1) {
		gpio_pad_select_gpio_x(amp_control.gpio);
		gpio_set_direction_x(amp_control.gpio, GPIO_MODE_OUTPUT);
		gpio_set_level_x(amp_control.gpio, !amp_control.active);
		LOG_INFO("setting amplifier GPIO %d (active:%d)", amp_control.gpio, amp_control.active);	
	}	

	if (jack_mutes_amp && jack_inserted_svc()) adac->speaker(false);
	else adac->speaker(true);
	
	adac->headset(jack_inserted_svc());	

	// create task as a FreeRTOS task but uses stack in internal RAM
	{
		static DRAM_ATTR StaticTask_t xTaskBuffer __attribute__ ((aligned (4)));
		static DRAM_ATTR StackType_t xStack[OUTPUT_THREAD_STACK_SIZE] __attribute__ ((aligned (4)));
		output_i2s_task = xTaskCreateStaticPinnedToCore( (TaskFunction_t) output_thread_i2s, "output_i2s", OUTPUT_THREAD_STACK_SIZE, 
											  NULL, CONFIG_ESP32_PTHREAD_TASK_PRIO_DEFAULT + 1, xStack, &xTaskBuffer, 0 );
	}
	
	// do we want stats
	p = config_alloc_get_default(NVS_TYPE_STR, "stats", "n", 0);
	stats = p && (*p == '1' || *p == 'Y' || *p == 'y');
	free(p);
	
	// memory still used but at least task is not created
	if (stats) {
		// we allocate TCB but stack is static to avoid SPIRAM fragmentation
		StaticTask_t* xTaskBuffer = (StaticTask_t*) heap_caps_malloc(sizeof(StaticTask_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
		static EXT_RAM_ATTR StackType_t xStack[STAT_STACK_SIZE] __attribute__ ((aligned (4)));
		stats_task = xTaskCreateStatic( (TaskFunction_t) output_thread_i2s_stats, "output_i2s_sts", STAT_STACK_SIZE, 
										 NULL, ESP_TASK_PRIO_MIN, xStack, xTaskBuffer);
	}	
}


/****************************************************************************************
 * Terminate DAC output
 */
void output_close_i2s(void) {
	LOCK;
	running = false;
	UNLOCK;
	
	while (!ended) vTaskDelay(20 / portTICK_PERIOD_MS);
	if (stats) vTaskDelete(stats_task);
	
	i2s_driver_uninstall(CONFIG_I2S_NUM);
	free(obuf);
	
	equalizer_close();
	
	adac->deinit();
}

/****************************************************************************************
 * change volume
 */
bool output_volume_i2s(unsigned left, unsigned right) {
	if (mute_control.gpio >= 0) gpio_set_level(mute_control.gpio, (left | right) ? !mute_control.active : mute_control.active);
	return adac->volume(left, right);
} 

/****************************************************************************************
 * Write frames to the output buffer
 */
static int _i2s_write_frames(frames_t out_frames, bool silence, s32_t gainL, s32_t gainR, u8_t flags,
								s32_t cross_gain_in, s32_t cross_gain_out, ISAMPLE_T **cross_ptr) {
	if (!silence) {
		if (output.fade == FADE_ACTIVE && output.fade_dir == FADE_CROSS && *cross_ptr) {
			_apply_cross(outputbuf, out_frames, cross_gain_in, cross_gain_out, cross_ptr);
		}
		
		_apply_gain(outputbuf, out_frames, gainL, gainR, flags);
		memcpy(obuf + oframes * BYTES_PER_FRAME, outputbuf->readp, out_frames * BYTES_PER_FRAME);
	} else {
		memcpy(obuf + oframes * BYTES_PER_FRAME, silencebuf, out_frames * BYTES_PER_FRAME);
	}

	// don't update visu if we don't have enough data in buffer
	if (silence || output.external || _buf_used(outputbuf) > outputbuf->size >> 2 ) {
		output_visu_export(obuf + oframes * BYTES_PER_FRAME, out_frames, output.current_sample_rate, silence, (gainL + gainR) / 2);
	}
		
	oframes += out_frames;
	
	return out_frames;
}

/****************************************************************************************
 * Main output thread
 */
static void output_thread_i2s(void *arg) {
	size_t bytes;
	frames_t iframes = FRAME_BLOCK;
	uint32_t timer_start = 0;
	int discard = 0;
	uint32_t fullness = gettime_ms();
	bool synced;
	output_state state = OUTPUT_OFF - 1;
	
	while (running) {
			
		TIME_MEASUREMENT_START(timer_start);

		LOCK;
		
		// manage led display & analogue
		if (state != output.state) {
			LOG_INFO("Output state is %d", output.state);
			if (output.state == OUTPUT_OFF) {
				led_blink(LED_GREEN, 100, 2500);
				if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, !amp_control.active);
				LOG_INFO("switching off amp GPIO %d", amp_control.gpio);
			} else if (output.state == OUTPUT_STOPPED) {
				adac->speaker(false);
				led_blink(LED_GREEN, 200, 1000);
			} else if (output.state == OUTPUT_RUNNING) {
				if (!jack_mutes_amp || !jack_inserted_svc()) {
					if (amp_control.gpio != -1) gpio_set_level_x(amp_control.gpio, amp_control.active);
					adac->speaker(true);
				}	
				led_on(LED_GREEN);
			}	
		}
		state = output.state;
		
		if (output.state == OUTPUT_OFF) {
			UNLOCK;
			if (isI2SStarted) {
				isI2SStarted = false;
				i2s_stop(CONFIG_I2S_NUM);
				adac->power(ADAC_STANDBY);
				spdif.count = 0;
			}
			usleep(100000);
			continue;
		} else if (output.state == OUTPUT_STOPPED) {
			synced = false;
		}
					
		oframes = 0;
		output.updated = gettime_ms();
		output.frames_played_dmp = output.frames_played;
		// try to estimate how much we have consumed from the DMA buffer (calculation is incorrect at the very beginning ...)
		output.device_frames = dma_buf_frames - ((output.updated - fullness) * output.current_sample_rate) / 1000;
		_output_frames( iframes );
		// oframes must be a global updated by the write callback
		output.frames_in_process = oframes;
						
		SET_MIN_MAX_SIZED(oframes,rec,iframes);
		SET_MIN_MAX_SIZED(_buf_used(outputbuf),o,outputbuf->size);
		SET_MIN_MAX_SIZED(_buf_used(streambuf),s,streambuf->size);
		SET_MIN_MAX( TIME_MEASUREMENT_GET(timer_start),buffering);
		
		/* must skip first whatever is in the pipe (but not when resuming). 
		This test is incorrect when we pause a track that has just started, 
		but this is higly unlikely and I don't have a better one for now */
		if (output.state == OUTPUT_START_AT) {
			discard = output.frames_played_dmp ? 0 : output.device_frames;
			synced = true;
		} else if (discard) {
			discard -= oframes;
			iframes = discard ? min(FRAME_BLOCK, discard) : FRAME_BLOCK;
			UNLOCK;
			continue;
		}
		
		UNLOCK;
				
		// now send all the data
		TIME_MEASUREMENT_START(timer_start);
		
		if (!isI2SStarted ) {
			isI2SStarted = true;
			LOG_INFO("Restarting I2S.");
			i2s_zero_dma_buffer(CONFIG_I2S_NUM);
			i2s_start(CONFIG_I2S_NUM);
			adac->power(ADAC_ON);	
		} 
		
		// this does not work well as set_sample_rates resets the fifos (and it's too early)
		if (i2s_config.sample_rate != output.current_sample_rate) {
			LOG_INFO("changing sampling rate %u to %u", i2s_config.sample_rate, output.current_sample_rate);
			if (synced) {
			/* 				
				//  can sleep for a buffer_queue - 1 and then eat a buffer (discard) if we are synced
				usleep(((DMA_BUF_COUNT - 1) * DMA_BUF_LEN * BYTES_PER_FRAME * 1000) / 44100 * 1000);
				discard = DMA_BUF_COUNT * DMA_BUF_LEN * BYTES_PER_FRAME;
			*/		
			}	
			i2s_config.sample_rate = output.current_sample_rate;
			i2s_set_sample_rates(CONFIG_I2S_NUM, spdif.enabled ? i2s_config.sample_rate * 2 : i2s_config.sample_rate);
			i2s_zero_dma_buffer(CONFIG_I2S_NUM);

#if BYTES_PER_FRAME == 4		
			equalizer_close();
			equalizer_open(output.current_sample_rate);
#endif			
		}
		
#if BYTES_PER_FRAME == 4		
		// run equalizer
		equalizer_process(obuf, oframes * BYTES_PER_FRAME, output.current_sample_rate);
#endif		

		// we assume that here we have been able to entirely fill the DMA buffers
		if (spdif.enabled) {
			size_t obytes, count = 0;
			bytes = 0;
			// need IRAM for speed but can't allocate a FRAME_BLOCK * 16, so process by smaller chunks
			while (count < oframes) {
				size_t chunk = min(SPDIF_BLOCK, oframes - count);
				spdif_convert((ISAMPLE_T*) obuf + count * 2, chunk, (u32_t*) spdif.buf, &spdif.count);
				i2s_write(CONFIG_I2S_NUM, spdif.buf, chunk * 16, &obytes, portMAX_DELAY);
				bytes += obytes / (16 / BYTES_PER_FRAME);
				count += chunk;
			}	
#if BYTES_PER_FRAME == 4		
		} else if (i2s_config.bits_per_sample == 32) {  
			i2s_write_expand(CONFIG_I2S_NUM, obuf, oframes * BYTES_PER_FRAME, 16, 32, &bytes, portMAX_DELAY);
#endif			
		} else {
			i2s_write(CONFIG_I2S_NUM, obuf, oframes * BYTES_PER_FRAME, &bytes, portMAX_DELAY);
		}

		fullness = gettime_ms();

		if (bytes != oframes * BYTES_PER_FRAME) {
			LOG_WARN("I2S DMA Overflow! available bytes: %d, I2S wrote %d bytes", oframes * BYTES_PER_FRAME, bytes);
		}
		
		SET_MIN_MAX( TIME_MEASUREMENT_GET(timer_start),i2s_time);
		
	}

	if (spdif.enabled) free(spdif.buf);
	ended = true;

	vTaskDelete(NULL);	
}

/****************************************************************************************
 * Stats output thread
 */
static void output_thread_i2s_stats(void *arg) {
	while (1) {
		// no need to lock
		output_state state = output.state;
		
		if(stats && state>OUTPUT_STOPPED){
			LOG_INFO( "Output State: %d, current sample rate: %d, bytes per frame: %d",state,output.current_sample_rate, BYTES_PER_FRAME);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD1);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD2);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD3);
			LOG_INFO( LINE_MIN_MAX_FORMAT_HEAD4);
			LOG_INFO(LINE_MIN_MAX_FORMAT_STREAM, LINE_MIN_MAX_STREAM("stream",s));
			LOG_INFO(LINE_MIN_MAX_FORMAT,LINE_MIN_MAX("output",o));
			LOG_INFO(LINE_MIN_MAX_FORMAT_FOOTER);
			LOG_INFO(LINE_MIN_MAX_FORMAT,LINE_MIN_MAX("received",rec));
			LOG_INFO(LINE_MIN_MAX_FORMAT_FOOTER);
			LOG_INFO("");
			LOG_INFO("              ----------+----------+-----------+-----------+  ");
			LOG_INFO("              max (us)  | min (us) |   avg(us) |  count    |  ");
			LOG_INFO("              ----------+----------+-----------+-----------+  ");
			LOG_INFO(LINE_MIN_MAX_DURATION_FORMAT,LINE_MIN_MAX_DURATION("Buffering(us)",buffering));
			LOG_INFO(LINE_MIN_MAX_DURATION_FORMAT,LINE_MIN_MAX_DURATION("i2s tfr(us)",i2s_time));
			LOG_INFO("              ----------+----------+-----------+-----------+");
			RESET_ALL_MIN_MAX;
		}
		vTaskDelay( pdMS_TO_TICKS( STATS_PERIOD_MS ) );
	}
}

/****************************************************************************************
 * SPDIF support
 */
 
#define PREAMBLE_B  (0xE8) //11101000
#define PREAMBLE_M  (0xE2) //11100010
#define PREAMBLE_W  (0xE4) //11100100

#define VUCP   		((0xCC) << 24)
#define VUCP_MUTE 	((0xD4) << 24)	// To mute PCM, set VUCP = invalid.

static const u16_t spdif_bmclookup[256] = { //biphase mark encoded values (least significant bit first)
	0xcccc, 0x4ccc, 0x2ccc, 0xaccc, 0x34cc, 0xb4cc, 0xd4cc, 0x54cc,
	0x32cc, 0xb2cc, 0xd2cc, 0x52cc, 0xcacc, 0x4acc, 0x2acc, 0xaacc,
	0x334c, 0xb34c, 0xd34c, 0x534c, 0xcb4c, 0x4b4c, 0x2b4c, 0xab4c,
	0xcd4c, 0x4d4c, 0x2d4c, 0xad4c, 0x354c, 0xb54c, 0xd54c, 0x554c,
	0x332c, 0xb32c, 0xd32c, 0x532c, 0xcb2c, 0x4b2c, 0x2b2c, 0xab2c,
	0xcd2c, 0x4d2c, 0x2d2c, 0xad2c, 0x352c, 0xb52c, 0xd52c, 0x552c,
	0xccac, 0x4cac, 0x2cac, 0xacac, 0x34ac, 0xb4ac, 0xd4ac, 0x54ac,
	0x32ac, 0xb2ac, 0xd2ac, 0x52ac, 0xcaac, 0x4aac, 0x2aac, 0xaaac,
	0x3334, 0xb334, 0xd334, 0x5334, 0xcb34, 0x4b34, 0x2b34, 0xab34,
	0xcd34, 0x4d34, 0x2d34, 0xad34, 0x3534, 0xb534, 0xd534, 0x5534,
	0xccb4, 0x4cb4, 0x2cb4, 0xacb4, 0x34b4, 0xb4b4, 0xd4b4, 0x54b4,
	0x32b4, 0xb2b4, 0xd2b4, 0x52b4, 0xcab4, 0x4ab4, 0x2ab4, 0xaab4,
	0xccd4, 0x4cd4, 0x2cd4, 0xacd4, 0x34d4, 0xb4d4, 0xd4d4, 0x54d4,
	0x32d4, 0xb2d4, 0xd2d4, 0x52d4, 0xcad4, 0x4ad4, 0x2ad4, 0xaad4,
	0x3354, 0xb354, 0xd354, 0x5354, 0xcb54, 0x4b54, 0x2b54, 0xab54,
	0xcd54, 0x4d54, 0x2d54, 0xad54, 0x3554, 0xb554, 0xd554, 0x5554,
	0x3332, 0xb332, 0xd332, 0x5332, 0xcb32, 0x4b32, 0x2b32, 0xab32,
	0xcd32, 0x4d32, 0x2d32, 0xad32, 0x3532, 0xb532, 0xd532, 0x5532,
	0xccb2, 0x4cb2, 0x2cb2, 0xacb2, 0x34b2, 0xb4b2, 0xd4b2, 0x54b2,
	0x32b2, 0xb2b2, 0xd2b2, 0x52b2, 0xcab2, 0x4ab2, 0x2ab2, 0xaab2,
	0xccd2, 0x4cd2, 0x2cd2, 0xacd2, 0x34d2, 0xb4d2, 0xd4d2, 0x54d2,
	0x32d2, 0xb2d2, 0xd2d2, 0x52d2, 0xcad2, 0x4ad2, 0x2ad2, 0xaad2,
	0x3352, 0xb352, 0xd352, 0x5352, 0xcb52, 0x4b52, 0x2b52, 0xab52,
	0xcd52, 0x4d52, 0x2d52, 0xad52, 0x3552, 0xb552, 0xd552, 0x5552,
	0xccca, 0x4cca, 0x2cca, 0xacca, 0x34ca, 0xb4ca, 0xd4ca, 0x54ca,
	0x32ca, 0xb2ca, 0xd2ca, 0x52ca, 0xcaca, 0x4aca, 0x2aca, 0xaaca,
	0x334a, 0xb34a, 0xd34a, 0x534a, 0xcb4a, 0x4b4a, 0x2b4a, 0xab4a,
	0xcd4a, 0x4d4a, 0x2d4a, 0xad4a, 0x354a, 0xb54a, 0xd54a, 0x554a,
	0x332a, 0xb32a, 0xd32a, 0x532a, 0xcb2a, 0x4b2a, 0x2b2a, 0xab2a,
	0xcd2a, 0x4d2a, 0x2d2a, 0xad2a, 0x352a, 0xb52a, 0xd52a, 0x552a,
	0xccaa, 0x4caa, 0x2caa, 0xacaa, 0x34aa, 0xb4aa, 0xd4aa, 0x54aa,
	0x32aa, 0xb2aa, 0xd2aa, 0x52aa, 0xcaaa, 0x4aaa, 0x2aaa, 0xaaaa
};

/* 
 SPDIF is supposed to be (before BMC encoding, from LSB to MSB)				
	PPPP AAAA  SSSS SSSS  SSSS SSSS  SSSS VUCP				
 after BMC encoding, each bits becomes 2 hence this becomes a 64 bits word. The
 the trick is to start not with a PPPP sequence but with an VUCP sequence to that
 the 16 bits samples are aligned with a BMC word boundary. Note that the LSB of the
 audio is transmitted first (not the MSB) and that ESP32 libray sends R then L, 
 contrary to what seems to be usually done, so (dst) order had to be changed
*/
static void IRAM_ATTR spdif_convert(ISAMPLE_T *src, size_t frames, u32_t *dst, size_t *count) {
	register u16_t hi, lo, aux;
	size_t cnt = *count;
	
	while (frames--) {
		// start with left channel
#if BYTES_PER_FRAME == 4		
		hi  = spdif_bmclookup[(u8_t)(*src >> 8)];
		lo  = spdif_bmclookup[(u8_t) *src++];
		
		// invert if last preceeding bit is 1
		lo ^= ~((s16_t)hi) >> 16;
		// first 16 bits
		*dst++ = ((u32_t)lo << 16) | hi;		
		aux = 0xb333 ^ (((u32_t)((s16_t)lo)) >> 17);
#else
		hi  = spdif_bmclookup[(u8_t)(*src >> 24)];
		lo  = spdif_bmclookup[(u8_t)(*src >> 16)];
		
		// invert if last preceeding bit is 1
		lo ^= ~((s16_t)hi) >> 16;
		// first 16 bits
		*dst++ = ((u32_t)lo << 16) | hi;		
		// we use 20 bits samples as we need to force parity
		aux = spdif_bmclookup[(u8_t)(*src++ >> 12)];
		aux = (u8_t) (aux ^ (~((s16_t)lo) >> 16));
		aux |= (0xb3 ^ (((u16_t)((s8_t)aux)) >> 9)) << 8;
#endif	
		
		// VUCP-Bits: Valid, Subcode, Channelstatus, Parity = 0
		// As parity is always 0, we can use fixed preambles
		if (++cnt > 191) {
			*dst++ =  VUCP | (PREAMBLE_B << 16 ) | aux; //special preamble for one of 192 frames
			cnt = 0;
		} else {
			*dst++ = VUCP | (PREAMBLE_M << 16) | aux;
		}

		// then do right channel, no need to check PREAMBLE_B
#if BYTES_PER_FRAME == 4		
		hi  = spdif_bmclookup[(u8_t)(*src >> 8)];
		lo  = spdif_bmclookup[(u8_t) *src++];
		lo ^= ~((s16_t)hi) >> 16;
		*dst++ = ((u32_t)lo << 16) | hi;
		aux = 0xb333 ^ (((u32_t)((s16_t)lo)) >> 17);
#else
		hi  = spdif_bmclookup[(u8_t)(*src >> 24)];
		lo  = spdif_bmclookup[(u8_t)(*src >> 16)];
		lo ^= ~((s16_t)hi) >> 16;
		*dst++ = ((u32_t)lo << 16) | hi;
		aux = spdif_bmclookup[(u8_t)(*src++ >> 12)];
		aux = (u8_t) (aux ^ (~((s16_t)lo) >> 16));
		aux |= (0xb3 ^ (((u16_t)((s8_t)aux)) >> 9)) << 8;
#endif	
		*dst++ = VUCP | (PREAMBLE_W << 16) | aux;
	}
	
	*count = cnt;
}





