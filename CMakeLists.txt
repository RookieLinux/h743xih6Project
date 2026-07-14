cmake_minimum_required(VERSION 3.15)

# The toolchain must be selected before project().  Use the RT-Thread Studio
# GCC 5.4.1 toolchain by default when CMake is invoked without -DCMAKE_TOOLCHAIN_FILE.
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/cmake/arm-none-eabi-gcc.cmake"
        CACHE FILEPATH "CMake toolchain file")
endif()

project(h743xih6Project LANGUAGES C ASM)

# Keep configuration flags deterministic across CMake versions. All relevant
# optimization/debug options are set on the firmware target below.
foreach(_config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set(CMAKE_C_FLAGS_${_config} "" CACHE STRING "" FORCE)
    set(CMAKE_ASM_FLAGS_${_config} "" CACHE STRING "" FORCE)
endforeach()

set(RT_STUDIO_LINKER_SCRIPT
    "${CMAKE_CURRENT_SOURCE_DIR}/linkscripts/STM32H743XIHx/link.lds" CACHE FILEPATH
    "Linker script used by RT-Thread Studio")

# Checked-in manifests make this build independent of RT-Thread Studio and its
# generated Debug directory.
include(cmake/components.cmake)
include(cmake/project_includes.cmake)

add_executable(rtthread ${FIRMWARE_SOURCES})
set_target_properties(rtthread PROPERTIES
    OUTPUT_NAME rtthread
    SUFFIX ".elf"
    C_STANDARD 11
    C_STANDARD_REQUIRED YES)

target_include_directories(rtthread PRIVATE ${FIRMWARE_INCLUDE_DIRS})
target_compile_definitions(rtthread PRIVATE
    SOC_FAMILY_STM32
    SOC_SERIES_STM32H7
    USE_HAL_DRIVER
    STM32H743xx)

set(_cpu_flags
    -mcpu=cortex-m7 -mthumb -mfloat-abi=hard -mfpu=fpv5-sp-d16)
target_compile_options(rtthread PRIVATE
    ${_cpu_flags}
    -ffunction-sections -fdata-sections -Wall
    "$<$<CONFIG:Debug>:-O0;-g;-gdwarf-2>"
    $<$<CONFIG:Release>:-O2>
    "$<$<CONFIG:RelWithDebInfo>:-O2;-g;-gdwarf-2>"
    $<$<CONFIG:MinSizeRel>:-Os>
    $<$<COMPILE_LANGUAGE:C>:-include${CMAKE_CURRENT_SOURCE_DIR}/rtconfig_preinc.h>)
target_link_options(rtthread PRIVATE
    ${_cpu_flags}
    -ffunction-sections -fdata-sections -Wall
    "$<$<CONFIG:Debug>:-g;-gdwarf-2>"
    "$<$<CONFIG:RelWithDebInfo>:-g;-gdwarf-2>"
    $<$<CONFIG:Release>:-Wl,--strip-debug>
    -T${RT_STUDIO_LINKER_SCRIPT}
    -Wl,--gc-sections
    -Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/rtthread.map
    -Wl,--cref)
target_link_libraries(rtthread PRIVATE c m)

add_custom_command(TARGET rtthread POST_BUILD
    COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:rtthread>
            ${CMAKE_CURRENT_BINARY_DIR}/rtthread.bin
    COMMAND ${CMAKE_SIZE} --format=berkeley $<TARGET_FILE:rtthread>
    COMMENT "Generating rtthread.bin and reporting image size")
