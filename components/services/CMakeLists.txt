idf_component_register(SRC_DIRS .
						INCLUDE_DIRS .
						REQUIRES json tools platform_config display wifi-manager esp_driver_i2c esp_driver_i2s
						PRIV_REQUIRES soc esp_hw_support driver esp_driver_rmt esp_driver_ledc app_trace
)
