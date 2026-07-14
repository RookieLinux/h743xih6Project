# CMake 构建说明

该工程的 CMake 构建不依赖 RT-Thread Studio，也不读取 `Debug` 目录。源码按组件动态发现，配置位于：

- `cmake/components.cmake`
- `cmake/components/*.cmake`
- `cmake/project_includes.cmake`

## Windows

本机默认优先使用 RT-Thread Studio 自带的 GNU Arm Embedded 5.4.1：

```powershell
cmake --preset rt-studio-debug
cmake --build --preset rt-studio-debug

cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

也可以通过环境变量指定其他 GNU Arm Embedded 工具链：

```powershell
$env:ARM_GCC_ROOT = "D:\Tools\gcc-arm-none-eabi"
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

## Ubuntu

安装构建工具和 GNU Arm Embedded 工具链：

```bash
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi
```

随后直接构建：

```bash
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

如果工具链不在 `PATH` 中：

```bash
export ARM_GCC_ROOT=/opt/gcc-arm-none-eabi-5_4-2016q3
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

Debug 输出位于 `build/cmake-debug`，Release 输出位于 `build/cmake-release`。每个目录都会生成 `rtthread.elf`、`rtthread.bin` 和 `rtthread.map`。

Release 使用 `-O2` 且不包含调试信息。若要求 Windows 和 Ubuntu 的固件最大程度保持一致，应在两个系统上使用相同版本的 GNU Arm Embedded 5.4.1。

在 `applications` 和 `drivers` 中新增或删除 C/汇编源文件会被 CMake、Ninja 和 CLion 自动识别。已注册的 RT-Thread、HAL、LVGL 和软件包源码目录也会自动识别文件变化。

新增全新的第三方组件目录时，需要在相应的 `cmake/components/*.cmake` 中注册目录；禁用同目录内的单个源码时，将它加入该组件的 `list(REMOVE_ITEM ...)` 排除清单。修改 `rtconfig.h` 后仍应检查组件注册和排除项是否与新配置一致。
