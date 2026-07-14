set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(ARM_GCC_ROOT "" CACHE PATH "GNU Arm Embedded toolchain root")
if(NOT ARM_GCC_ROOT AND DEFINED ENV{ARM_GCC_ROOT})
    file(TO_CMAKE_PATH "$ENV{ARM_GCC_ROOT}" ARM_GCC_ROOT)
endif()

# Preserve RT-Thread Studio's exact compiler on the original Windows machine.
set(_studio_gcc
    "D:/RT-ThreadStudio/repo/Extract/ToolChain_Support_Packages/ARM/GNU_Tools_for_ARM_Embedded_Processors/5.4.1")
if(NOT ARM_GCC_ROOT AND EXISTS "${_studio_gcc}/bin/arm-none-eabi-gcc.exe")
    set(ARM_GCC_ROOT "${_studio_gcc}")
endif()

set(_hints)
if(ARM_GCC_ROOT)
    list(APPEND _hints "${ARM_GCC_ROOT}/bin")
endif()
find_program(ARM_NONE_EABI_GCC arm-none-eabi-gcc HINTS ${_hints})
find_program(ARM_NONE_EABI_AR arm-none-eabi-ar HINTS ${_hints})
find_program(ARM_NONE_EABI_OBJCOPY arm-none-eabi-objcopy HINTS ${_hints})
find_program(ARM_NONE_EABI_SIZE arm-none-eabi-size HINTS ${_hints})
if(NOT ARM_NONE_EABI_GCC OR NOT ARM_NONE_EABI_AR OR
   NOT ARM_NONE_EABI_OBJCOPY OR NOT ARM_NONE_EABI_SIZE)
    message(FATAL_ERROR
        "GNU Arm Embedded tools were not found. Add their bin directory to PATH "
        "or set ARM_GCC_ROOT / the ARM_GCC_ROOT environment variable.")
endif()

set(CMAKE_C_COMPILER "${ARM_NONE_EABI_GCC}")
set(CMAKE_ASM_COMPILER "${ARM_NONE_EABI_GCC}")
set(CMAKE_AR "${ARM_NONE_EABI_AR}" CACHE FILEPATH "Archiver" FORCE)
set(CMAKE_OBJCOPY "${ARM_NONE_EABI_OBJCOPY}" CACHE FILEPATH "Objcopy" FORCE)
set(CMAKE_SIZE "${ARM_NONE_EABI_SIZE}" CACHE FILEPATH "Size utility" FORCE)

if(ARM_GCC_ROOT)
    set(CMAKE_FIND_ROOT_PATH "${ARM_GCC_ROOT}/arm-none-eabi")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
