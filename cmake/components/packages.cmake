# Source directories for the currently enabled component set.
set(packages_source_directories
    "packages/gt911-v1.0.0/src"
    "packages/rt_vsnprintf_full-latest"
)

collect_source_directories(PACKAGES_SOURCES ${packages_source_directories})

list(APPEND FIRMWARE_SOURCES ${PACKAGES_SOURCES})

