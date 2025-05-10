if(IDF_TARGET STREQUAL esp32 AND IDF_VERSION_MAJOR EQUAL 4 AND IDF_VERSION_MINOR LESS 4)
    set(lib_dir ${build_dir}/esp-idf)
    set(driver esp32/i2s.c)
    string(REPLACE ".c" ".c.obj" driver_obj "${driver}")

    idf_component_register( SRCS ${driver}
                        REQUIRES driver           
                        INCLUDE_DIRS ${IDF_PATH}/components/driver
                        PRIV_INCLUDE_DIRS ${IDF_PATH}/components/driver/include/driver
    )

    # CMake is just a pile of crap
    message(STATUS "!! overriding ${driver} !!")
    message(STATUS "CAREFUL, LIBRARIES STRIPPING FROM DUPLICATED COMPONENTS DEPENDS ON THIS BEING REBUILD")

    add_custom_command(
                TARGET ${COMPONENT_LIB}
                PRE_LINK
                COMMAND xtensa-esp32-elf-ar -d ${lib_dir}/driver/libdriver.a ${driver_obj}
                VERBATIM
    )
else()
    message(STATUS "==> NO OVERRIDE <==")
    # Register dummy component to keep IDF buildchain happy
    idf_component_register( SRCS ${driver}
        REQUIRES driver           
        INCLUDE_DIRS ""
        PRIV_INCLUDE_DIRS ""
)
endif()
