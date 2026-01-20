
idf_component_register(SRC_DIRS .    
						INCLUDE_DIRS .   
						PRIV_REQUIRES newlib freertos pthread platform_config espressif__mdns services codecs tools display wifi-manager
						  
)
set_source_files_properties(raop.c
    PROPERTIES COMPILE_FLAGS
    -Wno-misleading-indentation
)