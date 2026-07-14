# Source directories for the currently enabled component set.
set(rtthread_source_directories
    "rt-thread/components/dfs/filesystems/devfs"
    "rt-thread/components/dfs/filesystems/elmfat"
    "rt-thread/components/dfs/src"
    "rt-thread/components/drivers/i2c"
    "rt-thread/components/drivers/ipc"
    "rt-thread/components/drivers/misc"
    "rt-thread/components/drivers/rtc"
    "rt-thread/components/drivers/serial"
    "rt-thread/components/drivers/spi"
    "rt-thread/components/drivers/spi/sfud/src"
    "rt-thread/components/drivers/touch"
    "rt-thread/components/fal/samples/porting"
    "rt-thread/components/fal/src"
    "rt-thread/components/finsh"
    "rt-thread/components/libc/compilers/common"
    "rt-thread/components/libc/compilers/newlib"
    "rt-thread/components/libc/posix/delay"
    "rt-thread/components/libc/posix/pthreads"
    "rt-thread/components/utilities/ulog"
    "rt-thread/components/utilities/ulog/backend"
    "rt-thread/libcpu/arm/common"
    "rt-thread/libcpu/arm/cortex-m7"
    "rt-thread/src"
)

collect_source_directories(RTTHREAD_SOURCES ${rtthread_source_directories})

# Files in enabled directories that are disabled by rtconfig.h.
list(REMOVE_ITEM RTTHREAD_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/misc/adc.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/misc/dac.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/misc/pulse_encoder.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/misc/rt_drv_pwm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/misc/rt_inputcapture.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/rtc/alarm.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/serial/serial_v2.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/spi/enc28j60.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/spi/spi_msd.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/spi/spi_wifi_rw009.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/drivers/spi/spi-bit-ops.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/fal/samples/porting/fal_flash_stm32f2_port.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/components/utilities/ulog/backend/file_be.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/libcpu/arm/common/divsi3.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/libcpu/arm/cortex-m7/context_iar.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/libcpu/arm/cortex-m7/context_rvds.S"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/src/cpu.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/src/memheap.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/src/signal.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/rt-thread/src/slab.c"
)

list(APPEND FIRMWARE_SOURCES ${RTTHREAD_SOURCES})

