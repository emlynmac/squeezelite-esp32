/* 
 *  Squeezelite for esp32
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/i2s.h>
#include "driver/i2c.h"
#include "esp_log.h"
#include "adac.h"

static const char TAG[] = "WM8711BL";

static void speaker(bool active) { }
static void headset(bool active) { }
static bool volume(unsigned left, unsigned right) { return false; }
static void power(adac_power_e mode);
static bool init(char *config, int i2c_port_num, i2s_config_t *i2s_config);

static esp_err_t i2c_write_shadow(uint8_t reg, uint16_t val);
static uint16_t i2c_read_shadow(uint8_t reg);

static int WM8711BL;

const struct adac_s dac_wm8711bl = { "WM8711BL", init, adac_deinit, power, speaker, headset, volume };

// Initialization table for the 9-bit registers, defaults from datasheet
static uint16_t WM8711BL_REGVAL_TBL[9] = {
    0x0000, 0x0000, // R0/1 not used
	0X0079, // R2 Left volume
    0X0079, // R3 Right volume
    0X0008, // R4 Audio path control
    0X0008, // R5 Digital Audio path control
    0X009F, // R6 Power down control
    0X000A, // R7 Digital audio interface control
    0X0000, // R8 Sampling control
    0x0000  // R9 Active control
};

/****************************************************************************************
 * init
 */
static bool init(char *config, int i2c_port, i2s_config_t *i2s_config) {	 
	WM8711BL = adac_init(config, i2c_port);
	
	if (!WM8711BL) WM8711BL = 0x1a;
	ESP_LOGI(TAG, "WM8711BL detected @%d", WM8711BL);

	// Initialization sequence
    adac_write_byte(WM8711BL, 0x0F, 0); // Reset 
    i2c_write_shadow(5, 3);  // Un-mute DAC, set de-emphasis for 48kHz
    i2c_write_shadow(4, 16); // Disable line-in audio path, enable DAC audio path
    i2c_write_shadow(7, 2);  // Set 16 bit, i2s format, use external clock
    i2c_write_shadow(9, 1); // Activate

	// Configure system clk to GPIO0 for DAC MCLK input
    ESP_LOGI(TAG, "Configuring MCLK on GPIO0");
	PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
   	REG_WRITE(PIN_CTRL, 0xFFFFFFF0);
	
	return true;
}	

/****************************************************************************************
 * power
 */
static void power(adac_power_e mode) {
    switch (mode)
    {
    case ADAC_ON:
        i2c_write_shadow(6, 103); // Power all on
        i2c_write_shadow(5, 3);   // Unmute DAC
        break;

    case ADAC_OFF:
        i2c_write_shadow(5, 11);  // Mute DAC
        i2c_write_shadow(6, 255); // Power all off
        break;

    case ADAC_STANDBY:
        i2c_write_shadow(5, 11);  // Mute DAC
        i2c_write_shadow(6, 127); // Standby mode
        mbreak;
    }
}

/****************************************************************************************
 *  Write with custom reg/value structure
 */
 static esp_err_t i2c_write_shadow(uint8_t reg, uint16_t val) {
	WM8711BL_REGVAL_TBL[reg] = val;
	reg = (reg << 1) | ((val >> 8) & 0x01);
    val &= 0xff;  
	return adac_write_byte(WM8711BL, reg, val);
}

/****************************************************************************************
 *  Return local register value
 */
static uint16_t i2c_read_shadow(uint8_t reg) {
	return WM8711BL_REGVAL_TBL[reg];
}
