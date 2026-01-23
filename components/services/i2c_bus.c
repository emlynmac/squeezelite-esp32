/*
   I2C Bus Service for ESP-IDF 5.x
   
   This module provides a centralized I2C bus management using the new
   i2c_master.h driver API, replacing the legacy driver/i2c.h API.
*/

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "i2c_bus.h"

static const char *TAG = "i2c_bus";

static i2c_master_bus_handle_t s_bus_handle = NULL;
static uint32_t s_default_speed = 400000;

// Device handle cache for efficient repeated access
#define MAX_CACHED_DEVICES 8
static struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} s_device_cache[MAX_CACHED_DEVICES];
static int s_cached_device_count = 0;
static SemaphoreHandle_t s_mutex = NULL;

/****************************************************************************************
 * Get or create a cached device handle for the given I2C address
 */
static i2c_master_dev_handle_t get_cached_device(uint8_t addr, uint32_t speed_hz) {
    // Check cache first
    for (int i = 0; i < s_cached_device_count; i++) {
        if (s_device_cache[i].addr == addr) {
            return s_device_cache[i].handle;
        }
    }
    
    // Create new device handle
    if (s_bus_handle == NULL) {
        ESP_LOGE(TAG, "I2C bus not initialized");
        return NULL;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz ? speed_hz : s_default_speed,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(s_bus_handle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add I2C device 0x%02x: %s", addr, esp_err_to_name(ret));
        return NULL;
    }
    
    // Cache if space available
    if (s_cached_device_count < MAX_CACHED_DEVICES) {
        s_device_cache[s_cached_device_count].addr = addr;
        s_device_cache[s_cached_device_count].handle = dev_handle;
        s_cached_device_count++;
    }
    
    return dev_handle;
}

/****************************************************************************************
 * Initialize the system I2C bus
 */
esp_err_t i2c_bus_init(int port, int sda_io, int scl_io, uint32_t speed_hz) {
    if (s_bus_handle != NULL) {
        ESP_LOGW(TAG, "I2C bus already initialized");
        return ESP_OK;
    }
    
    if (sda_io < 0 || scl_io < 0) {
        ESP_LOGW(TAG, "Invalid I2C pins: sda=%d, scl=%d", sda_io, scl_io);
        return ESP_ERR_INVALID_ARG;
    }
    
    s_default_speed = speed_hz ? speed_hz : 400000;
    
    i2c_master_bus_config_t bus_config = {
        .i2c_port = port,
        .sda_io_num = sda_io,
        .scl_io_num = scl_io,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    
    esp_err_t ret = i2c_new_master_bus(&bus_config, &s_bus_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create I2C master bus: %s", esp_err_to_name(ret));
        s_bus_handle = NULL;
        return ret;
    }
    
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    
    ESP_LOGI(TAG, "I2C bus initialized: port=%d, sda=%d, scl=%d, speed=%lu Hz", 
             port, sda_io, scl_io, (unsigned long)speed_hz);
    
    return ESP_OK;
}

/****************************************************************************************
 * Deinitialize the system I2C bus
 */
void i2c_bus_deinit(void) {
    // Remove all cached devices
    for (int i = 0; i < s_cached_device_count; i++) {
        i2c_master_bus_rm_device(s_device_cache[i].handle);
    }
    s_cached_device_count = 0;
    
    if (s_bus_handle != NULL) {
        i2c_del_master_bus(s_bus_handle);
        s_bus_handle = NULL;
    }
}

/****************************************************************************************
 * Get the system I2C bus handle
 */
i2c_master_bus_handle_t i2c_bus_get_handle(void) {
    return s_bus_handle;
}

/****************************************************************************************
 * Check if the system I2C bus is initialized
 */
bool i2c_bus_is_initialized(void) {
    return s_bus_handle != NULL;
}

/****************************************************************************************
 * Add a device to the system I2C bus
 */
esp_err_t i2c_bus_add_device(uint8_t addr, uint32_t speed_hz, i2c_master_dev_handle_t *dev_handle) {
    if (s_bus_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = addr,
        .scl_speed_hz = speed_hz ? speed_hz : s_default_speed,
    };
    
    return i2c_master_bus_add_device(s_bus_handle, &dev_cfg, dev_handle);
}

/****************************************************************************************
 * Remove a device from the system I2C bus
 */
esp_err_t i2c_bus_remove_device(i2c_master_dev_handle_t dev_handle) {
    // Also remove from cache if present
    for (int i = 0; i < s_cached_device_count; i++) {
        if (s_device_cache[i].handle == dev_handle) {
            // Shift remaining entries
            for (int j = i; j < s_cached_device_count - 1; j++) {
                s_device_cache[j] = s_device_cache[j + 1];
            }
            s_cached_device_count--;
            break;
        }
    }
    
    return i2c_master_bus_rm_device(dev_handle);
}

/****************************************************************************************
 * Write data to an I2C device
 */
esp_err_t i2c_bus_write(uint8_t addr, uint8_t reg, const uint8_t *data, size_t len) {
    i2c_master_dev_handle_t dev = get_cached_device(addr, 0);
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    
    esp_err_t ret;
    
    if (reg == 0xFF) {
        // No register, write data directly
        ret = i2c_master_transmit(dev, data, len, 100);
    } else {
        // Allocate buffer for reg + data
        uint8_t *buf = malloc(len + 1);
        if (buf == NULL) return ESP_ERR_NO_MEM;
        
        buf[0] = reg;
        memcpy(buf + 1, data, len);
        
        ret = i2c_master_transmit(dev, buf, len + 1, 100);
        free(buf);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C write to 0x%02x failed: %s", addr, esp_err_to_name(ret));
    }
    
    return ret;
}

/****************************************************************************************
 * Write a single byte to an I2C device register
 */
esp_err_t i2c_bus_write_byte(uint8_t addr, uint8_t reg, uint8_t val) {
    i2c_master_dev_handle_t dev = get_cached_device(addr, 0);
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    
    uint8_t data[2] = { reg, val };
    esp_err_t ret = i2c_master_transmit(dev, data, sizeof(data), 100);
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C write byte to 0x%02x failed: %s", addr, esp_err_to_name(ret));
    }
    
    return ret;
}

/****************************************************************************************
 * Read data from an I2C device
 */
esp_err_t i2c_bus_read(uint8_t addr, uint8_t reg, uint8_t *data, size_t len) {
    i2c_master_dev_handle_t dev = get_cached_device(addr, 0);
    if (dev == NULL) return ESP_ERR_INVALID_STATE;
    
    esp_err_t ret;
    
    if (reg == 0xFF) {
        // No register, read directly
        ret = i2c_master_receive(dev, data, len, 100);
    } else {
        // Write register, then read
        ret = i2c_master_transmit_receive(dev, &reg, 1, data, len, 100);
    }
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C read from 0x%02x failed: %s", addr, esp_err_to_name(ret));
    }
    
    return ret;
}

/****************************************************************************************
 * Read a single byte from an I2C device register
 */
uint8_t i2c_bus_read_byte(uint8_t addr, uint8_t reg) {
    uint8_t data = 0xFF;
    
    i2c_master_dev_handle_t dev = get_cached_device(addr, 0);
    if (dev == NULL) return data;
    
    esp_err_t ret = i2c_master_transmit_receive(dev, &reg, 1, &data, 1, 100);
    
    if (ret != ESP_OK) {
        ESP_LOGD(TAG, "I2C read byte from 0x%02x failed: %s", addr, esp_err_to_name(ret));
    }
    
    return data;
}
