# Source directories for the currently enabled component set.
set(packages_source_directories
    "packages/gt911-v1.0.0/src"
    "packages/rt_vsnprintf_full-latest"
)

collect_source_directories(PACKAGES_SOURCES ${packages_source_directories})

# RW007 sources selected by the active rtconfig.h. Keep these explicit because
# the package's src directory also contains optional BLE sources.
list(APPEND PACKAGES_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/rw007-v2.1.0/src/spi_wifi_rw007.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/packages/rw007-v2.1.0/example/rw007_stm32_port.c"
)

list(APPEND FIRMWARE_SOURCES ${PACKAGES_SOURCES})

