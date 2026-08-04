# CMake Build Guide

[简体中文](CMAKE.md)

The CMake build does not depend on RT-Thread Studio and does not read files
from the `Debug` directory. Sources are discovered by component. The relevant
configuration files are:

- `cmake/components.cmake`
- `cmake/components/*.cmake`
- `cmake/project_includes.cmake`

## Windows

By default, the build first looks for the GNU Arm Embedded 5.4.1 toolchain
bundled with RT-Thread Studio:

```powershell
cmake --preset rt-studio-debug
cmake --build --preset rt-studio-debug

cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

You can select another GNU Arm Embedded toolchain through an environment
variable:

```powershell
$env:ARM_GCC_ROOT = "D:\Tools\gcc-arm-none-eabi"
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

## Ubuntu

Install CMake, Ninja, and the GNU Arm Embedded toolchain:

```bash
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi
```

Then build the Release preset:

```bash
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

If the toolchain is not available through `PATH`, set its installation root:

```bash
export ARM_GCC_ROOT=/opt/gcc-arm-none-eabi-5_4-2016q3
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

Debug output is written to `build/cmake-debug`, and Release output is written
to `build/cmake-release`. Each directory contains `rtthread.elf`,
`rtthread.bin`, and `rtthread.map` after a successful build.

The Release preset uses `-O2` and does not include debug information. For the
closest possible binary reproducibility between Windows and Ubuntu, use the
same GNU Arm Embedded 5.4.1 toolchain version on both systems.

Adding or removing C or assembly sources under `applications` and `drivers` is
detected automatically by CMake, Ninja, and CLion. Registered RT-Thread, HAL,
LVGL, and package source directories also detect file changes automatically.
The current package manifest includes uMQTT, WebClient, and cJSON for the
MQTT/HTTP OTA implementation.

When adding a new third-party component directory, register it in the
appropriate `cmake/components/*.cmake` file. To disable an individual source
from an otherwise enabled directory, add it to that component's
`list(REMOVE_ITEM ...)` exclusion list. After changing `rtconfig.h`, verify
that the component registrations and exclusions still match the enabled
configuration.
