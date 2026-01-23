/**
 * Copyright (c) 2017-2018 Tara Keeling
 * 
 * This software is released under the MIT License.
 * https://opensource.org/licenses/MIT
 */

#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include "gds.h"
#include "gds_err.h"
#include "gds_private.h"
#include "gds_default_if.h"

static i2c_master_bus_handle_t I2CBusHandle = NULL;
static int I2CTimeout;

static const int GDS_I2C_COMMAND_MODE = 0x80;
static const int GDS_I2C_DATA_MODE = 0x40;

// Device handle cache for display devices
#define MAX_DISPLAY_DEVICES 4
static struct {
    uint8_t addr;
    i2c_master_dev_handle_t handle;
} DisplayDeviceCache[MAX_DISPLAY_DEVICES];
static int CachedDeviceCount = 0;

static i2c_master_dev_handle_t GetDeviceHandle(uint8_t address);
static bool I2CDefaultWriteBytes( int Address, bool IsCommand, const uint8_t* Data, size_t DataLength );
static bool I2CDefaultWriteCommand( struct GDS_Device* Device, uint8_t Command );
static bool I2CDefaultWriteData( struct GDS_Device* Device, const uint8_t* Data, size_t DataLength );

/*
 * Get or create a device handle for the given I2C address
 */
static i2c_master_dev_handle_t GetDeviceHandle(uint8_t address) {
    // Check cache first
    for (int i = 0; i < CachedDeviceCount; i++) {
        if (DisplayDeviceCache[i].addr == address) {
            return DisplayDeviceCache[i].handle;
        }
    }
    
    // Create new device handle
    if (I2CBusHandle == NULL) {
        return NULL;
    }
    
    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = 400000,
    };
    
    i2c_master_dev_handle_t dev_handle;
    esp_err_t ret = i2c_master_bus_add_device(I2CBusHandle, &dev_cfg, &dev_handle);
    if (ret != ESP_OK) {
        return NULL;
    }
    
    // Cache if space available
    if (CachedDeviceCount < MAX_DISPLAY_DEVICES) {
        DisplayDeviceCache[CachedDeviceCount].addr = address;
        DisplayDeviceCache[CachedDeviceCount].handle = dev_handle;
        CachedDeviceCount++;
    }
    
    return dev_handle;
}

/*
 * Initializes the i2c master with the parameters specified
 * in the component configuration in sdkconfig.h.
 * 
 * Returns true on successful init of the i2c bus.
 */
bool GDS_I2CInit( int PortNumber, int SDA, int SCL, int Speed ) {
    I2CTimeout = Speed ? (250 * 250000) / Speed : 250;
    
    if (SDA != -1 && SCL != -1) {
        i2c_master_bus_config_t bus_config = {
            .i2c_port = PortNumber,
            .sda_io_num = SDA,
            .scl_io_num = SCL,
            .clk_source = I2C_CLK_SRC_DEFAULT,
            .glitch_ignore_cnt = 7,
            .flags.enable_internal_pullup = true,
        };
        
        esp_err_t ret = i2c_new_master_bus(&bus_config, &I2CBusHandle);
        if (ret != ESP_OK) {
            return false;
        }
    }

    return true;
}

/*
 * Attaches a display to the I2C bus using default communication functions.
 * 
 * Params:
 * Device: Pointer to your GDS_Device object
 * Width: Width of display
 * Height: Height of display
 * I2CAddress: Address of your display
 * RSTPin: Optional GPIO pin to use for hardware reset, if none pass -1 for this parameter.
 * 
 * Returns true on successful init of display.
 */
bool GDS_I2CAttachDevice( struct GDS_Device* Device, int Width, int Height, int I2CAddress, int RSTPin, int BacklightPin ) {
    NullCheck( Device, return false );

    Device->WriteCommand = I2CDefaultWriteCommand;
    Device->WriteData = I2CDefaultWriteData;
    Device->Address = I2CAddress;
    Device->RSTPin = RSTPin;
    Device->Backlight.Pin = BacklightPin;
    Device->IF = GDS_IF_I2C;
    Device->Width = Device->TextWidth = Width;
    Device->Height = Height;
    
    if ( RSTPin >= 0 ) {
        ESP_ERROR_CHECK_NONFATAL( gpio_set_direction( RSTPin, GPIO_MODE_OUTPUT ), return false );
        ESP_ERROR_CHECK_NONFATAL( gpio_set_level( RSTPin, 1 ), return false );
        GDS_Reset( Device );
    }
    
    return GDS_Init( Device );
}

static bool I2CDefaultWriteBytes( int Address, bool IsCommand, const uint8_t* Data, size_t DataLength ) {
    NullCheck( Data, return false );
    
    i2c_master_dev_handle_t dev = GetDeviceHandle(Address);
    if (dev == NULL) {
        return false;
    }
    
    // Allocate buffer for mode byte + data
    uint8_t* buf = malloc(DataLength + 1);
    if (buf == NULL) {
        return false;
    }
    
    buf[0] = IsCommand ? GDS_I2C_COMMAND_MODE : GDS_I2C_DATA_MODE;
    memcpy(buf + 1, Data, DataLength);
    
    esp_err_t ret = i2c_master_transmit(dev, buf, DataLength + 1, I2CTimeout);
    free(buf);
    
    return ret == ESP_OK;
}

static bool I2CDefaultWriteCommand( struct GDS_Device* Device, uint8_t Command ) {
    uint8_t CommandByte = ( uint8_t ) Command;
    
    NullCheck( Device, return false );
    return I2CDefaultWriteBytes( Device->Address, true, ( const uint8_t* ) &CommandByte, 1 );
}

static bool I2CDefaultWriteData( struct GDS_Device* Device, const uint8_t* Data, size_t DataLength ) {
    NullCheck( Device, return false );
    NullCheck( Data, return false );

    return I2CDefaultWriteBytes( Device->Address, false, Data, DataLength );
}
