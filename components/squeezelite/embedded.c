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
#include <setjmp.h>
#include "squeezelite.h"
#include "pthread.h"
#include "esp_pthread.h"
#include "esp_system.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "monitor.h"
#include "platform_config.h"
#include "messaging.h"
#include "gpio_exp.h"
#include "accessors.h"

#ifndef CONFIG_POWER_GPIO_LEVEL
#define CONFIG_POWER_GPIO_LEVEL 1
#endif

static const char TAG[] = "embedded";

static struct {
	int gpio, active;
} power_control = { CONFIG_POWER_GPIO, CONFIG_POWER_GPIO_LEVEL };

extern void sb_controls_init(void);
extern bool sb_displayer_init(void);

u8_t custom_player_id = 12;

mutex_type slimp_mutex;
static jmp_buf jumpbuf;

#ifndef POWER_LOCKED
static void set_power_gpio(int gpio, char *value) {
	if (strcasestr(value, "power")) {
        char *p = strchr(value, ':');
		if (p) power_control.active = atoi(p + 1);
		power_control.gpio = gpio;        
	}	
}
#endif

void get_mac(u8_t mac[]) {
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
}

_sig_func_ptr signal(int sig, _sig_func_ptr func) {
	return NULL;
}

void em_logprint(const char *fmt, ...) {
    va_list args;
	va_start(args, fmt);
	vfprintf(stderr, fmt, args);    
    vmessaging_post_message(MESSAGING_ERROR, MESSAGING_CLASS_SYSTEM, fmt, args); 
	va_end(args);
	fflush(stderr);
}

void *audio_calloc(size_t nmemb, size_t size) {
	return calloc(nmemb, size);
}

int	pthread_create_name(pthread_t *thread, _CONST pthread_attr_t  *attr, 
				   void *(*start_routine)( void * ), void *arg, char *name) {
	esp_pthread_cfg_t cfg = esp_pthread_get_default_config(); 
	cfg.thread_name = name; 
	cfg.inherit_cfg = true;
	
	// Get stack size from attr if provided
	size_t stack_size = 0;
	if (attr) {
		pthread_attr_getstacksize(attr, &stack_size);
	}
	
	size_t internal_free = heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
	size_t spiram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
	
	ESP_LOGI(TAG, "Creating thread '%s' (stack: %zu), heap internal: %zu, SPIRAM: %zu",
			 name, stack_size, internal_free, spiram_free);
	
	// Use SPIRAM for thread stacks since internal RAM is fragmented
	// This requires CONFIG_SPIRAM_ALLOW_STACK_EXTERNAL_MEMORY=y
	cfg.stack_alloc_caps = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
	
	esp_pthread_set_cfg(&cfg); 
	int ret = pthread_create(thread, attr, start_routine, arg);
	
	if (ret != 0) {
		ESP_LOGE(TAG, "Failed to create thread '%s'! error=%d, heap internal: %zu, SPIRAM: %zu",
				 name, ret, heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
				 heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	}
	
	return ret;
}

uint32_t _gettime_ms_(void) {
	return (uint32_t) (esp_timer_get_time() / 1000);
}

int embedded_init(void) {
	mutex_create(slimp_mutex);
	sb_controls_init();
	custom_player_id = sb_displayer_init() ? 100 : 101;
    
#ifndef POWER_LOCKED
	parse_set_GPIO(set_power_gpio);
#endif

	if (power_control.gpio != -1) {
		gpio_pad_select_gpio_x(power_control.gpio);
		gpio_set_direction_x(power_control.gpio, GPIO_MODE_OUTPUT);
		gpio_set_level_x(power_control.gpio, !power_control.active);
		ESP_LOGI(TAG, "setting power GPIO %d (active:%d)", power_control.gpio, power_control.active);	
	}	    
    
    return setjmp(jumpbuf);
}

void embedded_exit(int code) {
    longjmp(jumpbuf, code + 1);
}    

void powering(bool on) {
    if (power_control.gpio != -1) {
        ESP_LOGI(TAG, "powering player %s", on ? "ON" : "OFF");	
        gpio_set_level_x(power_control.gpio, on ? power_control.active : !power_control.active);
    }
}

u16_t get_RSSI(void) {
    wifi_ap_record_t wifidata;
    esp_wifi_sta_get_ap_info(&wifidata);
	// we'll assume dBm, -30 to -100
    if (wifidata.primary != 0) return 100 + wifidata.rssi + 30;
    else return 0xffff;
}	

u16_t get_plugged(void) {
    return jack_inserted_svc() ? PLUG_HEADPHONE : 0;
}

u16_t get_battery(void) {
	return (u16_t) (battery_value_svc() * 128) & 0x0fff;
}	 

void set_name(char *name) {
	char *cmd = config_alloc_get(NVS_TYPE_STR, "autoexec1");
	char *p, *q;
	
	if (!cmd) return;

	if ((p = strstr(cmd, " -n")) != NULL) {
		q = p + 3;
		// in case some smart dude has a " -" in player's name
		while ((q = strstr(q, " -")) != NULL) {
			if (!strchr(q, '"') || !strchr(q+1, '"')) break;
			q++;
		}
		if (q) memmove(p, q, strlen(q) + 1);
		else *p = '\0';
	}

	asprintf(&q, "%s -n \"%s\"", cmd, name);
    config_set_value(NVS_TYPE_STR, "autoexec1", q);
	
	free(q);
	free(cmd);
}
