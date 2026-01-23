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
 
#include <string.h> 
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "driver/i2s_std.h"
#include "i2c_bus.h"
#include "esp_log.h"
#include "adac.h"

#define PARSE_PARAM(S,P,C,V) do {									\
	char *__p;														\
	if ((__p = strcasestr(S, P)) && (__p = strchr(__p, C))) V = atoi(__p+1); \
} while (0)

static const char TAG[] = "DAC core";

/****************************************************************************************
 * init
 */
int adac_init(char *config, int i2c_port_num) {	 
	int i2c_addr = 0;
	int sda_io = -1;
	int scl_io = -1;

	PARSE_PARAM(config, "i2c", '=', i2c_addr);
	PARSE_PARAM(config, "sda", '=', sda_io);
	PARSE_PARAM(config, "scl", '=', scl_io);

	if (sda_io == -1 || scl_io == -1) {
		ESP_LOGW(TAG, "DAC does not use i2c");
		return i2c_addr;
	}	
	
	ESP_LOGI(TAG, "DAC uses I2C port:%d, sda:%d, scl:%d", i2c_port_num, sda_io, scl_io);
	
	// Check if the centralized I2C bus is already initialized
	if (!i2c_bus_is_initialized()) {
		// Initialize the centralized I2C bus if not already done
		esp_err_t ret = i2c_bus_init(i2c_port_num, sda_io, scl_io, 250000);
		if (ret != ESP_OK) {
			ESP_LOGE(TAG, "Failed to initialize I2C bus: %s", esp_err_to_name(ret));
			return 0;
		}
	} else {
		ESP_LOGI(TAG, "Using already initialized I2C bus");
	}
	
	return i2c_addr;
}	

/****************************************************************************************
 * close
 */
void adac_deinit(void) {
	// The centralized I2C bus is managed by services, don't deinit here
}	

/****************************************************************************************
 * 
 */
esp_err_t adac_write_byte(int i2c_addr, uint8_t reg, uint8_t val) {
	return i2c_bus_write_byte(i2c_addr, reg, val);
}

/****************************************************************************************
 * 
 */
uint8_t adac_read_byte(int i2c_addr, uint8_t reg) {
	return i2c_bus_read_byte(i2c_addr, reg);
}

/****************************************************************************************
 * 
 */
uint16_t adac_read_word(int i2c_addr, uint8_t reg) {
	uint8_t data[2] = { 255, 255 };
	
	esp_err_t ret = i2c_bus_read(i2c_addr, reg, data, 2);
	
	if (ret != ESP_OK) {
		ESP_LOGW(TAG, "I2C read word failed: %s", esp_err_to_name(ret));
		return 0xFFFF;
	}

	return (data[0] << 8) | data[1];
}

/****************************************************************************************
 * 
 */
esp_err_t adac_write_word(int i2c_addr, uint8_t reg, uint16_t val) {
	uint8_t data[2] = { val >> 8, val & 0xff };
	return i2c_bus_write(i2c_addr, reg, data, 2);
}

/****************************************************************************************
 * 
 */
esp_err_t adac_write(int i2c_addr, uint8_t reg, uint8_t *data, size_t count) {
	return i2c_bus_write(i2c_addr, reg, data, count);
}	