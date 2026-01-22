/**
 * Stub implementations for missing ESP-IDF 5.5.1 symbols
 * These functions are referenced by ESP-IDF libraries but not implemented
 * when certain features are disabled.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "esp_app_format.h"
#include "esp_log.h"

static const char *TAG = "esp_idf_stubs";

// WPS Registrar stubs (not used in client mode)
void* wps_registrar_process_msg(void* wps, uint8_t op_code, void* msg) {
    ESP_LOGD(TAG, "wps_registrar_process_msg() stub called - WPS registrar not implemented");
    return NULL;
}

void* wps_registrar_get_msg(void* wps, uint8_t* op_code) {
    ESP_LOGD(TAG, "wps_registrar_get_msg() stub called - WPS registrar not implemented");
    return NULL;
}

// BT SMP Secure Connections stubs (advanced pairing features)
void* smp_get_local_oob_data(void) {
    ESP_LOGD(TAG, "smp_get_local_oob_data() stub called - SMP OOB not implemented");
    return NULL;
}

bool smp_encrypt_data(uint8_t* key, uint8_t key_len,
                      uint8_t* plain_text, uint8_t pt_len,
                      uint8_t* cipher_text) {
    ESP_LOGD(TAG, "smp_encrypt_data() stub called - SMP encryption not implemented");
    return false;
}

void smp_clear_local_oob_data(void) {
    ESP_LOGD(TAG, "smp_clear_local_oob_data() stub called - no-op");
}

void smp_calculate_long_term_key_from_link_key(void* p_cb) {
    ESP_LOGD(TAG, "smp_calculate_long_term_key_from_link_key() stub called - no-op");
}

void smp_save_secure_connections_long_term_key(void* p_cb) {
    ESP_LOGD(TAG, "smp_save_secure_connections_long_term_key() stub called - no-op");
}

// Bootloader image header stub
// This is normally only available in bootloader context, but bootloader_flash_config_esp32.c
// references it in the application. Provide a stub with safe default values.
esp_image_header_t __attribute__((section(".dram1.data"))) bootloader_image_hdr = {
    .magic = ESP_IMAGE_HEADER_MAGIC,
    .segment_count = 0,
    .spi_mode = 0,
    .spi_speed = 0,
    .spi_size = 0,
    .entry_addr = 0,
    .wp_pin = 0xEE, // Default WP pin disabled
    .spi_pin_drv = {0, 0, 0},
    .chip_id = 0,
    .min_chip_rev = 0,
    .min_chip_rev_full = 0,
    .max_chip_rev_full = 0,
    .reserved = {0, 0, 0, 0},
    .hash_appended = 0};

// PHY parameter tracking stub (referenced by phy_common.c)
// This is a function to track PHY parameters for WiFi and BLE
void phy_param_track_tot(bool en_wifi, bool en_ble_154)
{
    ESP_LOGW(TAG, "phy_param_track_tot(wifi=%d, ble_154=%d) stub called - no-op", en_wifi, en_ble_154);
}

// RTC clock init stubs (referenced by librtc.a)
// These functions are referenced by esp_phy/lib/esp32/librtc.a (prebuilt binary)
// but were removed from ESP-IDF 5.x. The prebuilt PHY library was compiled against
// an older SDK version and still expects these symbols.
void rtc_init_clk(void) {
    ESP_LOGW(TAG, "rtc_init_clk() stub called - no-op");
}

void rtc_slp_prep(void) {
    ESP_LOGW(TAG, "rtc_slp_prep() stub called - no-op");
}
