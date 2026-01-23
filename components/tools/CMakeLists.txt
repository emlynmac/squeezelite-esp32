idf_component_register( SRCS operator.cpp tools.c trace.c
						REQUIRES esp_common pthread esp_timer
						PRIV_REQUIRES esp_http_client esp-tls
						INCLUDE_DIRS .
)

#doing our own implementation of new operator for some pre-compiled binaries
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _ZdlPv")
target_link_libraries(${COMPONENT_LIB} INTERFACE "-u _Znwj")

  