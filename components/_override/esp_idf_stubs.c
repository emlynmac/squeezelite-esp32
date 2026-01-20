/**
 * Stub implementations for missing ESP-IDF 5.5.1 symbols
 * These functions are referenced by ESP-IDF libraries but not implemented
 * when certain features are disabled.
 */

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

// WPS Registrar stubs (not used in client mode)
void* wps_registrar_process_msg(void* wps, uint8_t op_code, void* msg) {
    return NULL;
}

void* wps_registrar_get_msg(void* wps, uint8_t* op_code) {
    return NULL;
}

// BT SMP Secure Connections stubs (advanced pairing features)
void* smp_get_local_oob_data(void) {
    return NULL;
}

bool smp_encrypt_data(uint8_t* key, uint8_t key_len,
                      uint8_t* plain_text, uint8_t pt_len,
                      uint8_t* cipher_text) {
    return false;
}

void smp_clear_local_oob_data(void) {
    // No-op
}

void smp_calculate_long_term_key_from_link_key(void* p_cb) {
    // No-op
}

void smp_save_secure_connections_long_term_key(void* p_cb) {
    // No-op
}

// Bootloader image header stub (referenced by bootloader_flash_config_esp32.c)
// This is normally provided by bootloader component, but seems missing in ESP-IDF 5.5.1
typedef struct {
    uint8_t magic;
    uint8_t segment_count;
    uint8_t spi_mode;
    uint8_t spi_speed: 4;
    uint8_t spi_size: 4;
    uint32_t entry_addr;
    uint8_t wp_pin;
    uint8_t spi_pin_drv[3];
    uint16_t chip_id;
    uint8_t min_chip_rev;
    uint16_t min_chip_rev_full;
    uint16_t max_chip_rev_full;
    uint8_t reserved[4];
    uint8_t hash_appended;
} esp_image_header_t;

esp_image_header_t bootloader_image_hdr = {
    .magic = 0xE9,
    .segment_count = 0,
    .spi_mode = 0,
    .spi_speed = 0,
    .spi_size = 0,
    .entry_addr = 0,
    .wp_pin = 0xFF,
    .spi_pin_drv = {0, 0, 0},
    .chip_id = 0,
    .min_chip_rev = 0,
    .min_chip_rev_full = 0,
    .max_chip_rev_full = 0,
    .reserved = {0, 0, 0, 0},
    .hash_appended = 0
};

// PHY parameter tracking stub (referenced by phy_common.c)
// This is a function to track PHY parameters for WiFi and BLE
void phy_param_track_tot(bool en_wifi, bool en_ble_154)
{
    // No-op - PHY parameter tracking not needed
}

// RTC clock init stubs (referenced by librtc.a)
void rtc_init_clk(void) {
    // No-op - RTC clock initialization handled elsewhere
}

void rtc_slp_prep(void) {
    // No-op - RTC sleep preparation handled elsewhere
}

// ESP-NETIF PPP authentication stub (PPP support disabled)
int esp_netif_ppp_set_auth_internal(void* netif, int authtype, const char* user, const char* passwd) {
    return ESP_ERR_NOT_SUPPORTED;
}

// mbedTLS TLS 1.3 stub (TLS 1.3 not enabled)
int mbedtls_ssl_tls13_handshake_client_step(void* ssl) {
    return -1; // MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE
}

// ============================================================================
// IRAM-placed libc function implementations to fix relocation errors
// These need to be in IRAM to avoid "call target out of range" linker errors
// when SPIRAM is enabled without the cache workaround.
// ============================================================================

#include <string.h>
#include <stdlib.h>
#include <reent.h>
#include <sys/lock.h>
#include "esp_attr.h"

// Simple IRAM itoa/utoa implementations (weak to avoid conflicts with libc)
__attribute__((weak)) IRAM_ATTR char* __itoa(int value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    int tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "zyxwvutsrqponmlkjihgfedcba9876543210123456789abcdefghijklmnopqrstuvwxyz"[35 + (tmp_value - value * base)];
    } while (value);
    
    if (tmp_value < 0) *ptr++ = '-';
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

__attribute__((weak)) IRAM_ATTR char* __utoa(unsigned value, char* str, int base) {
    char* ptr = str;
    char* ptr1 = str;
    char tmp_char;
    unsigned tmp_value;
    
    if (base < 2 || base > 36) {
        *str = '\0';
        return str;
    }
    
    do {
        tmp_value = value;
        value /= base;
        *ptr++ = "0123456789abcdefghijklmnopqrstuvwxyz"[tmp_value - value * base];
    } while (value);
    
    *ptr-- = '\0';
    
    while (ptr1 < ptr) {
        tmp_char = *ptr;
        *ptr-- = *ptr1;
        *ptr1++ = tmp_char;
    }
    return str;
}

// IRAM strtok_r implementation (weak to avoid conflicts with libc)
__attribute__((weak)) IRAM_ATTR char* __strtok_r(char* s, const char* delim, char** lasts) {
    char* spanp;
    int c, sc;
    char* tok;
    
    if (s == NULL && (s = *lasts) == NULL)
        return NULL;
    
cont:
    c = *s++;
    for (spanp = (char*)delim; (sc = *spanp++) != 0;) {
        if (c == sc)
            goto cont;
    }
    
    if (c == 0) {
        *lasts = NULL;
        return NULL;
    }
    tok = s - 1;
    
    for (;;) {
        c = *s++;
        spanp = (char*)delim;
        do {
            if ((sc = *spanp++) == c) {
                if (c == 0)
                    s = NULL;
                else
                    s[-1] = 0;
                *lasts = s;
                return tok;
            }
        } while (sc != 0);
    }
}

// IRAM lock functions for stdio (weak to avoid conflicts with libc)
__attribute__((weak)) IRAM_ATTR void __sfp_lock_acquire(void) {
    // Simplified - in full implementation would acquire lock
}

__attribute__((weak)) IRAM_ATTR void __sfp_lock_release(void) {
    // Simplified - in full implementation would release lock
}

