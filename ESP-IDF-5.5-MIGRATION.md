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
- **Why it exists**: 
  - **Custom feature**: Adds `fixed_mclk` field to `i2s_config_t` structure (line 101)
  - **Purpose**: Allows setting a fixed MCLK (master clock) frequency for audio DACs
  - **Usage**: When `use_apll` is true and `fixed_mclk` is set, it forces a specific MCLK frequency (lines 458-460)
  - **Benefit**: Provides precise clock control needed for high-quality audio output with external DACs
  - **Build system**: Only active for ESP32 with IDF 4.x < 4.4 (see [components/_override/CMakeLists.txt](components/_override/CMakeLists.txt))
  
- **Migration Options**:

  #### Option 1: Port the Custom Feature to New I2S API (Recommended)
  ESP-IDF 5.x has a completely new I2S driver architecture. You'll need to:
  
  1. **Understand the new API structure**:
     ```c
     // Old API (ESP-IDF 4.x)
     i2s_driver_install(i2s_port_t, i2s_config_t*, queue_size, queue_handle);
     i2s_set_pin(i2s_port_t, i2s_pin_config_t*);
     i2s_write(i2s_port_t, buffer, size, &bytes_written, timeout);
     
     // New API (ESP-IDF 5.x)
     i2s_new_channel(&chan_cfg, &tx_handle, &rx_handle);
     i2s_channel_init_std_mode(tx_handle, &std_cfg);
     i2s_channel_enable(tx_handle);
     i2s_channel_write(tx_handle, buffer, size, &bytes_written, timeout);
     ```
  
  2. **Create a custom clock configuration**:
     - ESP-IDF 5.x allows MCLK configuration through `i2s_std_clk_config_t`
     - The `mclk_multiple` field controls MCLK frequency
     - You may need to use `i2s_channel_reconfig_std_clock()` to achieve similar functionality
     - Check if the new API's clock configuration is sufficient without the override
  
  3. **Update all I2S calls in the application**:
     - [components/squeezelite/output_i2s.c](components/squeezelite/output_i2s.c) - Main audio output (uses `i2s_write`, `i2s_driver_install`)
     - [components/squeezelite/adac.h](components/squeezelite/adac.h) - DAC interface definitions
     - All DAC drivers in `components/squeezelite/*/` that use I2S
  
  4. **Test APLL clock generation**:
     - Verify APLL settings work correctly with new API
     - The clock calculation logic may need adjustment
  
  #### Option 2: Use Stock ESP-IDF 5.x I2S Driver
  - Try using the standard I2S driver without modifications
  - Configure MCLK through the new `i2s_std_clk_config_t.mclk_multiple` field
  - May lose some precision but simpler migration path
  - Test if audio quality is acceptable
  
  #### Option 3: Keep Driver Override for ESP-IDF 5.x (Advanced)
  - Port the entire i2s.c driver from ESP-IDF 5.x and add the `fixed_mclk` feature
  - This is complex as the driver architecture changed significantly
  - Would require updating HAL layer calls as well
  - **Not recommended** due to maintenance burden

- **Required Actions**:
  1. ✅ Review why `fixed_mclk` is needed (documented above)
  2. ⬜ Test if ESP-IDF 5.x stock I2S driver with `mclk_multiple` provides sufficient control
  3. ⬜ If insufficient, port the custom feature to new I2S API
  4. ⬜ Update [components/_override/CMakeLists.txt](components/_override/CMakeLists.txt) to handle ESP-IDF 5.x
  5. ⬜ Update all application code using I2S (primarily `output_i2s.c`)
  6. ⬜ Test audio output thoroughly on actual hardware
  
- **Key Code Locations**:
  - Custom field definition: [i2s.c line 101](components/_override/esp32/i2s.c#L101)
  - Clock calculation: [i2s.c lines 458-460](components/_override/esp32/i2s.c#L458-L460)
  - Field initialization: [i2s.c line 889](components/_override/esp32/i2s.c#L889)
  - Main I2S usage: [output_i2s.c](components/squeezelite/output_i2s.c)
  
- **Migration guides**: 
  - https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/migration-guides/release-5.x/5.0/peripherals.html#i2s-driver
  - https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/i2s.html

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
- [I2S Driver Documentation (ESP-IDF 5.x)](https://docs.espressif.com/projects/esp-idf/en/v5.5/esp32/api-reference/peripherals/i2s.html)

## 🔧 Practical I2S Migration Example

Here's a basic example of how to update I2S code from ESP-IDF 4.x to 5.x:

### ESP-IDF 4.x Code (Current):
```c
#include "driver/i2s.h"

i2s_config_t i2s_config = {
    .mode = I2S_MODE_MASTER | I2S_MODE_TX,
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .dma_buf_count = 8,
    .dma_buf_len = 512,
    .use_apll = true,
    .tx_desc_auto_clear = true,
    .fixed_mclk = 11289600  // CUSTOM FIELD - NOT IN STOCK ESP-IDF
};

i2s_pin_config_t pin_config = {
    .bck_io_num = 26,
    .ws_io_num = 25,
    .data_out_num = 22,
    .data_in_num = -1
};

i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
i2s_set_pin(I2S_NUM_0, &pin_config);

// Write audio data
size_t bytes_written;
i2s_write(I2S_NUM_0, audio_buffer, buffer_size, &bytes_written, portMAX_DELAY);
```

### ESP-IDF 5.x Code (Target):
```c
#include "driver/i2s_std.h"

i2s_chan_handle_t tx_handle;

// Step 1: Configure the channel
i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
chan_cfg.dma_desc_num = 8;
chan_cfg.dma_frame_num = 512;
chan_cfg.auto_clear = true;

i2s_new_channel(&chan_cfg, &tx_handle, NULL);

// Step 2: Configure the standard mode
i2s_std_config_t std_cfg = {
    .clk_cfg = {
        .sample_rate_hz = 44100,
        .clk_src = I2S_CLK_SRC_APLL,  // Use APLL
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,  // MCLK = sample_rate * 256
    },
    .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
    .gpio_cfg = {
        .mclk = I2S_GPIO_UNUSED,  // Or specify MCLK pin if needed
        .bclk = 26,
        .ws = 25,
        .dout = 22,
        .din = I2S_GPIO_UNUSED,
        .invert_flags = {
            .mclk_inv = false,
            .bclk_inv = false,
            .ws_inv = false,
        },
    },
};

i2s_channel_init_std_mode(tx_handle, &std_cfg);
i2s_channel_enable(tx_handle);

// Write audio data
size_t bytes_written;
i2s_channel_write(tx_handle, audio_buffer, buffer_size, &bytes_written, portMAX_DELAY);

// To achieve fixed_mclk behavior, you may need:
// - Use I2S_CLK_SRC_APLL for precise clocking
// - Calculate mclk_multiple based on your required MCLK frequency
// - Or reconfigure clock dynamically with i2s_channel_reconfig_std_clock()
```

### For Custom MCLK (Equivalent to fixed_mclk):
```c
// If you need precise MCLK control (equivalent to the fixed_mclk override):

// Option 1: Set mclk_multiple
// MCLK = sample_rate * mclk_multiple
// For 44.1kHz with MCLK of 11.2896MHz: mclk_multiple = 256

std_cfg.clk_cfg.mclk_multiple = I2S_MCLK_MULTIPLE_256;

// Option 2: Dynamically reconfigure
i2s_std_clk_config_t clk_cfg = {
    .sample_rate_hz = 44100,
    .clk_src = I2S_CLK_SRC_APLL,
    .mclk_multiple = I2S_MCLK_MULTIPLE_256,
};
i2s_channel_reconfig_std_clock(tx_handle, &clk_cfg);

// Note: The new API may not provide the exact same control as the custom
// fixed_mclk field. You may need to experiment with different mclk_multiple
// values or APLL settings to achieve the desired clock frequency.
```

### Key Differences:
1. **Two-step initialization**: Create channel first, then configure mode
2. **Separate handles**: TX and RX have separate handles instead of port number
3. **New function names**: `i2s_channel_write()` instead of `i2s_write()`
4. **Clock configuration**: More structured with `clk_cfg`, `slot_cfg`, `gpio_cfg`
5. **MCLK control**: Use `mclk_multiple` instead of custom `fixed_mclk`

## 🚀 Next Steps

1. **CRITICAL**: Address the I2S driver override issue
2. **HIGH**: Update ADC driver in battery.c
3. **MEDIUM**: Test build with ESP-IDF 5.5
4. **MEDIUM**: Fix any compilation errors
5. **LOW**: Update Dockerfile to use ESP-IDF 5.5 base image
6. **LOW**: Review and update component dependencies
7. **ONGOING**: Test all functionality thoroughly

## 📝 Detailed Action Plan

### Phase 1: I2S Driver Migration (Critical - Estimate: 2-4 days)
1. **Research** (2-4 hours):
   - [ ] Read ESP-IDF 5.x I2S migration guide thoroughly
   - [ ] Review new I2S API examples in ESP-IDF repository
   - [ ] Understand `mclk_multiple` and APLL configuration options
   - [ ] Test if `mclk_multiple` can replace `fixed_mclk` functionality

2. **Decision** (1 hour):
   - [ ] Decide: Use stock I2S driver OR port custom feature
   - [ ] Document decision rationale

3. **Implementation** (1-2 days):
   - [ ] Update `output_i2s.c` to use new I2S API
   - [ ] Update all DAC driver initialization code
   - [ ] Remove or update `_override` component
   - [ ] Handle MCLK configuration (mck_io_num → gpio_cfg.mclk)
   - [ ] Update SPDIF output code if applicable

4. **Testing** (1-2 days):
   - [ ] Test audio playback at different sample rates
   - [ ] Test APLL clock accuracy
   - [ ] Test with different DAC models (tas57xx, ac101, wm8978, etc.)
   - [ ] Verify SPDIF output if used
   - [ ] Check for audio glitches or timing issues

### Phase 2: ADC Driver Update (Medium - Estimate: 2-4 hours)
1. **Update battery.c**:
   - [ ] Include new header: `#include "esp_adc/adc_oneshot.h"`
   - [ ] Replace `adc1_config_width()` with oneshot unit configuration
   - [ ] Replace `adc1_config_channel_atten()` with oneshot channel configuration
   - [ ] Replace `adc1_get_raw()` with `adc_oneshot_read()`
   - [ ] Handle ADC handle lifecycle (init/deinit)

2. **Test**:
   - [ ] Verify battery voltage readings are accurate
   - [ ] Test battery level percentage calculation

### Phase 3: Build and Initial Testing (Medium - Estimate: 1-2 days)
1. **Environment Setup**:
   - [ ] Install ESP-IDF 5.5: `git clone -b v5.5 https://github.com/espressif/esp-idf --recursive`
   - [ ] Run installer: `./install.sh esp32,esp32s3`
   - [ ] Source environment: `. ./export.sh`

2. **Build**:
   - [ ] Run `idf.py menuconfig` and review new options
   - [ ] Attempt build: `idf.py build`
   - [ ] Fix compilation errors iteratively
   - [ ] Check for deprecation warnings

3. **Flash and Test**:
   - [ ] Flash to device: `idf.py flash monitor`
   - [ ] Verify boot sequence
   - [ ] Check for runtime errors in logs

### Phase 4: Functional Testing (High - Estimate: 2-3 days)
1. **Network**:
   - [ ] WiFi STA connection
   - [ ] WiFi AP mode
   - [ ] Ethernet (if applicable)
   - [ ] mDNS advertisement
   - [ ] Web UI access

2. **Audio**:
   - [ ] Squeezelite playback (various formats)
   - [ ] Different sample rates (44.1, 48, 88.2, 96, 192 kHz)
   - [ ] Volume control
   - [ ] Audio controls (play/pause/skip)
   - [ ] Bluetooth audio (if used)
   - [ ] AirPlay/RAOP (if used)
   - [ ] Spotify Connect (if used)

3. **Peripherals**:
   - [ ] Display output (if applicable)
   - [ ] LED indicators
   - [ ] Rotary encoder
   - [ ] Buttons
   - [ ] Battery monitoring

4. **System**:
   - [ ] OTA updates
   - [ ] NVS configuration
   - [ ] Deep sleep/wake
   - [ ] Stability (run for 24+ hours)

### Phase 5: Component Updates (Low - Estimate: 1 day)
1. **External Components**:
   - [ ] Check esp-dsp compatibility
   - [ ] Update Spotify/cspot if needed
   - [ ] Check other submodules

2. **Documentation**:
   - [ ] Update build instructions
   - [ ] Update troubleshooting guide
   - [ ] Document any breaking changes for users

### Phase 6: Docker and CI/CD (Low - Estimate: 2-4 hours)
1. **Docker**:
   - [ ] Update Dockerfile to use ESP-IDF 5.5 base
   - [ ] Test Docker build process
   - [ ] Update README with new Docker commands

2. **GitHub Actions** (if applicable):
   - [ ] Update CI workflow to use ESP-IDF 5.5
   - [ ] Verify automated builds pass

## 🎯 Success Criteria

The migration is complete when:
- ✅ Project builds without errors on ESP-IDF 5.5
- ✅ All audio playback works correctly (all sample rates, all DAC models)
- ✅ Network functionality works (WiFi, mDNS, Web UI)
- ✅ All peripherals work (display, buttons, LEDs, etc.)
- ✅ Battery monitoring works (if applicable)
- ✅ OTA updates work
- ✅ System is stable for 24+ hours of continuous operation
- ✅ No regression in audio quality or timing
- ✅ Documentation is updated

## ⚠️ Risk Assessment

**HIGH RISK**:
- I2S audio quality degradation if MCLK control is lost
- Timing issues in audio pipeline due to I2S driver changes
- APLL clock accuracy differences

**MEDIUM RISK**:
- Unexpected API behavior changes in peripheral drivers
- Memory usage increase (ESP-IDF 5.x uses more RAM)
- Build time increase

**LOW RISK**:
- Minor configuration option changes
- Documentation gaps

## 📞 Getting Help

If you encounter issues:
1. Check [ESP-IDF GitHub Issues](https://github.com/espressif/esp-idf/issues)
2. Search [ESP32 Forum](https://esp32.com/)
3. Review [Squeezelite-ESP32 GitHub Issues](https://github.com/philippe44/squeezelite-esp32/issues)
4. Compare with [official ESP-IDF examples](https://github.com/espressif/esp-idf/tree/v5.5/examples)

## Notes

- The migration focused on the most critical breaking changes
- Some APIs may have additional optional parameters in ESP-IDF 5.5
- Consider enabling compiler warnings to catch deprecated API usage
- Run `idf.py menuconfig` to review new configuration options in ESP-IDF 5.5
