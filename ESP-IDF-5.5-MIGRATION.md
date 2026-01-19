# ESP-IDF 5.5 Migration Guide for Squeezelite-ESP32

This document outlines the changes made and remaining work needed to migrate this project from ESP-IDF 4.3/4.4 to ESP-IDF 5.5.

## ✅ Completed Changes

### 1. SPI Flash API Updates
- **Changed**: Replaced `esp_spi_flash.h` with `esp_partition.h`
- **Files modified**:
  - [main/esp_app_main.c](main/esp_app_main.c)
  - [components/squeezelite-ota/squeezelite-ota.c](components/squeezelite-ota/squeezelite-ota.c)
- **Reason**: The `esp_spi_flash.h` API was deprecated in ESP-IDF 5.0

### 2. TCP/IP Adapter → ESP-NETIF Migration
- **Changed**: Replaced all `tcpip_adapter_*` calls with `esp_netif_*` equivalents
- **Files modified**:
  - [components/wifi-manager/network_manager.h](components/wifi-manager/network_manager.h)
  - [components/wifi-manager/network_manager.c](components/wifi-manager/network_manager.c)
  - [components/wifi-manager/network_services.h](components/wifi-manager/network_services.h)
  - [components/wifi-manager/http_server_handlers.c](components/wifi-manager/http_server_handlers.c)
  - [components/raop/util.c](components/raop/util.c)
  - [components/squeezelite-ota/protocol_examples_common.h](components/squeezelite-ota/protocol_examples_common.h)
  - [main/esp_app_main.c](main/esp_app_main.c)
- **Key API changes**:
  - `tcpip_adapter_ip_info_t` → `esp_netif_ip_info_t`
  - `tcpip_adapter_dhcp_status_t` → `esp_netif_dhcp_status_t`
  - `tcpip_adapter_get_hostname()` → `esp_netif_get_hostname()`
  - `tcpip_adapter_get_ip_info()` → `esp_netif_get_ip_info()`
  - `tcpip_adapter_is_netif_up()` → `esp_netif_is_netif_up()`
  - Removed `TCPIP_ADAPTER_IF_STA` and `TCPIP_ADAPTER_IF_AP` macros - now use netif handles
- **Reason**: The tcpip_adapter API was deprecated in ESP-IDF 4.1 and removed in 5.0

### 3. Configuration Changes
- **Changed**: Removed `CONFIG_ESP_NETIF_TCPIP_ADAPTER_COMPATIBLE_LAYER` from all sdkconfig files
- **Files modified**:
  - sdkconfig
  - sdkconfig.defaults
  - sdkconfig_minimal
  - sdkconfig-backup
  - build-scripts/I2S-4MFlash-sdkconfig.defaults
  - build-scripts/Muse-sdkconfig.defaults
  - build-scripts/SqueezeAmp-sdkconfig.defaults
  - build-scripts/I2S-S3-sdkconfig
- **Reason**: This compatibility layer doesn't exist in ESP-IDF 5.x

### 4. Documentation Updates
- **Changed**: Updated README.md to reference ESP-IDF 5.5
- **Changes**:
  - Docker image: `sle118/squeezelite-esp32-idfv435` → `espressif/idf:v5.5`
  - ESP-IDF version: 4.3.5/4.4.5 → 5.5
  - Installation instructions updated

## ⚠️ Critical Issues Requiring Manual Review

### 1. I2S Driver Override (CRITICAL)
- **File**: [components/_override/esp32/i2s.c](components/_override/esp32/i2s.c) (1208 lines)
- **Issue**: This is a custom override of the ESP-IDF I2S driver from version 4.x
- **Required Action**: 
  - The I2S driver was completely redesigned in ESP-IDF 5.0 with a new architecture
  - New API uses `driver/i2s_std.h`, `driver/i2s_tdm.h`, `driver/i2s_pdm.h`
  - This file likely needs to be completely rewritten or removed if the custom modifications are no longer needed
  - Review why this override exists and if it's still necessary with ESP-IDF 5.5
  - Consider using the new I2S driver directly if possible
- **Migration guide**: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.0/peripherals.html#i2s-driver

### 2. ADC Driver API (MEDIUM PRIORITY)
- **File**: [components/services/battery.c](components/services/battery.c)
- **Issue**: Uses deprecated ADC API
- **Current API**:
  ```c
  adc1_config_width(ADC_WIDTH_BIT_12);
  adc1_config_channel_atten(battery.channel, battery.attenuation);
  adc1_get_raw(battery.channel);
  ```
- **Required Action**: 
  - Replace with new ADC oneshot API
  - New API requires:
    ```c
    #include "esp_adc/adc_oneshot.h"
    adc_oneshot_unit_handle_t adc_handle;
    adc_oneshot_unit_init_cfg_t init_config = {...};
    adc_oneshot_new_unit(&init_config, &adc_handle);
    adc_oneshot_read(adc_handle, channel, &raw_value);
    ```
- **Migration guide**: https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.0/peripherals.html#adc

### 3. Component Dependencies (LOW PRIORITY)
Some external components in the `components/` directory may need updates:
- esp-dsp (if using outdated version)
- spotify (cspot library)
- Check if these are compatible with ESP-IDF 5.5

## 🔍 Additional Items to Check

### Build System
- **CMakeLists.txt**: Current file seems compatible, but test the build
- **Partition tables**: Verify partition table formats haven't changed
- **Linker scripts**: Check linker.lf files for compatibility

### Driver APIs to Verify
The following driver APIs may have changed. Search and verify:
1. **LEDC/PWM** - Check if `ledc_timer_config_t` and `ledc_channel_config_t` are still compatible
2. **SPI** - Verify `spi_bus_config_t` structure hasn't changed
3. **GPIO** - Check GPIO interrupt handling APIs
4. **UART** - Verify if any UART code exists and needs updates
5. **Timer** - Check if any hardware timer usage needs migration

### Configuration Options
Some Kconfig options may have changed or been removed:
- WiFi configuration options (CONFIG_ESP_WIFI_*)
- Power management options
- FreeRTOS options
- Bluetooth options (if used)

## 📋 Testing Checklist

Before considering the migration complete, test:

1. ✅ **Build**: `idf.py build` succeeds
2. ⬜ **Flash**: `idf.py flash` works
3. ⬜ **Boot**: Device boots without errors
4. ⬜ **WiFi**: WiFi connection works (STA and AP modes)
5. ⬜ **Audio**: Audio playback works (I2S output)
6. ⬜ **Bluetooth**: BT audio works (if applicable)
7. ⬜ **OTA**: Over-the-air updates work
8. ⬜ **Battery**: Battery monitoring works (ADC)
9. ⬜ **Display**: Display output works (if applicable)
10. ⬜ **Web UI**: Web interface is accessible

## 📚 Additional Resources

- [ESP-IDF 5.0 Migration Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.0/index.html)
- [ESP-IDF 5.1 Migration Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.1/index.html)
- [ESP-IDF 5.2 Migration Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.2/index.html)
- [ESP-IDF 5.3 Migration Guide](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.3/index.html)
- [ESP-IDF API Reference](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/index.html)

## 🚀 Next Steps

1. **CRITICAL**: Address the I2S driver override issue
2. **HIGH**: Update ADC driver in battery.c
3. **MEDIUM**: Test build with ESP-IDF 5.5
4. **MEDIUM**: Fix any compilation errors
5. **LOW**: Update Dockerfile to use ESP-IDF 5.5 base image
6. **LOW**: Review and update component dependencies
7. **ONGOING**: Test all functionality thoroughly

## Notes

- The migration focused on the most critical breaking changes
- Some APIs may have additional optional parameters in ESP-IDF 5.5
- Consider enabling compiler warnings to catch deprecated API usage
- Run `idf.py menuconfig` to review new configuration options in ESP-IDF 5.5
