/*
   I2C Bus Service for ESP-IDF 5.x
   
   This module provides a centralized I2C bus management using the new
   i2c_master.h driver API, replacing the legacy driver/i2c.h API.
*/

#pragma once

#include "esp_err.h"
#include "driver/i2c_master.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the system I2C bus
 * 
 * @param port I2C port number
 * @param sda_io SDA GPIO number
 * @param scl_io SCL GPIO number
 * @param speed_hz Clock speed in Hz
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_init(int port, int sda_io, int scl_io, uint32_t speed_hz);

/**
 * @brief Deinitialize the system I2C bus
 */
void i2c_bus_deinit(void);

/**
 * @brief Get the system I2C bus handle
 * 
 * @return Bus handle or NULL if not initialized
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void);

/**
 * @brief Check if the system I2C bus is initialized
 * 
 * @return true if initialized
 */
bool i2c_bus_is_initialized(void);

/**
 * @brief Add a device to the system I2C bus
 * 
 * @param addr 7-bit I2C address
 * @param speed_hz Device clock speed (0 to use bus default)
 * @param dev_handle Output device handle
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_add_device(uint8_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev_handle);

/**
 * @brief Remove a device from the system I2C bus
 * 
 * @param dev_handle Device handle to remove
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_remove_device(i2c_master_dev_handle_t dev_handle);

/**
 * @brief Write data to an I2C device
 * 
 * @param addr 7-bit I2C address
 * @param reg Register address (use 0xFF to skip register write)
 * @param data Data to write
 * @param len Number of bytes to write
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len);

/**
 * @brief Write a single byte to an I2C device register
 * 
 * @param addr 7-bit I2C address
 * @param reg Register address
 * @param val Value to write
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_write_byte(uint8_t addr, uint8_t reg, uint8_t val);

/**
 * @brief Read data from an I2C device
 * 
 * @param addr 7-bit I2C address
 * @param reg Register address (use 0xFF to skip register write)
 * @param data Buffer to read into
 * @param len Number of bytes to read
 * @return ESP_OK on success
 */
esp_err_t i2c_bus_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len);

/**
 * @brief Read a single byte from an I2C device register
 * 
 * @param addr 7-bit I2C address
 * @param reg Register address
 * @return Value read, or 0xFF on error
 */
uint8_t i2c_bus_read_byte(uint8_t addr, uint8_t reg);

#ifdef __cplusplus
}
#endif
