idf_build_get_property(prefix IDF_PATH)
string(CONCAT prefix "${prefix}" "/components/esp_http_server")

idf_component_register(
		SRC_DIRS "${prefix}/src" "${prefix}/src/util"	
		INCLUDE_DIRS "${prefix}/include"
        PRIV_INCLUDE_DIRS "." "${prefix}/src/port/esp32" "${prefix}/src/util"
        REQUIRES http_parser esp_event # for http_parser.h (was nghttp in ESP-IDF 4.x) and esp_event.h
        PRIV_REQUIRES lwip mbedtls esp_timer
) 
