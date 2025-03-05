idf_component_register(SRC_DIRS .
						INCLUDE_DIRS .
						REQUIRES json tools platform_config display wifi-manager
						PRIV_REQUIRES soc # esp32 removed/refactored in v5 
)
