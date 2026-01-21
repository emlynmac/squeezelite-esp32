/* 
 *  Squeezelite for esp32
 *
 *  (c) Philippe G. 2019, philippe_44@outlook.com
 *
 *  This software is released under the MIT License.
 *  https://opensource.org/licenses/MIT
 *
 */
 
#pragma once

#define I2C_SYSTEM_PORT		1
#define SPI_SYSTEM_HOST		SPI2_HOST

extern int i2c_system_port;
extern int i2c_system_speed;
extern int spi_system_host;
extern int spi_system_dc_gpio;
typedef struct {
	int timer, base_channel, max;
} pwm_system_t;
extern pwm_system_t pwm_system;
