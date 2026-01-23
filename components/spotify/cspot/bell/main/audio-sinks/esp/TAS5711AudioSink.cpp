#include "TAS5711AudioSink.h"
#include "i2c_bus.h"

struct tas5711_cmd_s {
  uint8_t reg;
  uint8_t value;
};

static const struct tas5711_cmd_s tas5711_init_sequence[] = {
    {0x00, 0x6c},  // 0x6c - 256 x mclk
    {0x04, 0x03},  // 0x03 - 16 bit i2s
    {0x05, 0x00},  // system control 0x00 is audio playback
    {0x06, 0x00},  // disable mute
    {0x07, 0x50},  // volume register
    {0xff, 0xff}

};

TAS5711AudioSink::TAS5711AudioSink() {
  i2s_config_t i2s_config = {

      .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),  // Only TX
      .sample_rate = 44100,
      .bits_per_sample = (i2s_bits_per_sample_t)16,
      .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,  //2-channels
      .communication_format = (i2s_comm_format_t)I2S_COMM_FORMAT_STAND_MSB,
      .intr_alloc_flags = 0,  //Default interrupt priority
      .dma_buf_count = 8,
      .dma_buf_len = 512,
      .use_apll = true,
      .tx_desc_auto_clear = true,  //Auto clear tx descriptor on underflow
      .fixed_mclk = 256 * 44100};

  i2s_pin_config_t pin_config = {
      .bck_io_num = 5,
      .ws_io_num = 25,
      .data_out_num = 26,
      .data_in_num = -1  //Not used
  };
  i2s_driver_install((i2s_port_t)0, &i2s_config, 0, NULL);
  i2s_set_pin((i2s_port_t)0, &pin_config);

  // Initialize I2C bus if not already done (uses centralized i2c_bus service)
  if (!i2c_bus_is_initialized()) {
    i2c_bus_init(0, 21, 23, 250000);  // Port 0, SDA=21, SCL=23, 250kHz
  }
  
  // Try to detect the TAS5711
  uint8_t data;
  esp_err_t ret = i2c_bus_read(TAS5711_ADDR, 0x00, &data, 1);

  if (ret == ESP_OK) {
    ESP_LOGI("RR", "Detected TAS");
  } else {
    ESP_LOGI("RR", "Unable to detect dac");
  }

  writeReg(0x1b, 0x00);
  vTaskDelay(100 / portTICK_PERIOD_MS);

  for (int i = 0; tas5711_init_sequence[i].reg != 0xff; i++) {
    writeReg(tas5711_init_sequence[i].reg, tas5711_init_sequence[i].value);
    vTaskDelay(1 / portTICK_PERIOD_MS);
  }

  startI2sFeed();
}

void TAS5711AudioSink::writeReg(uint8_t reg, uint8_t value) {
  esp_err_t res = i2c_bus_write_byte(TAS5711_ADDR, reg, value);

  if (res != ESP_OK) {
    ESP_LOGE("RR", "Unable to write to TAS5711");
  }
}

TAS5711AudioSink::~TAS5711AudioSink() {}
