
idf_component_register(SRC_DIRS .    
						INCLUDE_DIRS .   
						PRIV_REQUIRES bootloader_support mbedtls esp_netif newlib freertos pthread tools platform_config mdns services codecs display wifi-manager
						  
)
set_source_files_properties(raop.c
    PROPERTIES COMPILE_FLAGS
    -Wno-misleading-indentation
)