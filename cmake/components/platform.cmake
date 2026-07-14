# Source directories for the currently enabled component set.
set(platform_source_directories
    "libraries/CMSIS/Device/ST/STM32H7xx/Source/Templates"
    "libraries/CMSIS/Device/ST/STM32H7xx/Source/Templates/gcc"
    "libraries/STM32H7xx_HAL_Driver/Src"
)

collect_source_directories(PLATFORM_SOURCES ${platform_source_directories})

list(APPEND FIRMWARE_SOURCES ${PLATFORM_SOURCES})

