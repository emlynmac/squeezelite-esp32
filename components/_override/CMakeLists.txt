
    # Create component with ESP-IDF 5.5.1 stubs for missing symbols
    idf_component_register(
        SRCS "esp_idf_stubs.c"
        PRIV_REQUIRES bootloader_support
        )
