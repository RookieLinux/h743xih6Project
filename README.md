# STM32H743XIH6 RT-Thread + LVGL 工程

这是一个面向 STM32H743XIH6 的 RT-Thread 嵌入式工程，支持使用 RT-Thread Studio 或 CMake/CLion 进行开发和构建。

工程目前完成了 800 × 480 RGB LCD、SDRAM 帧缓冲、LTDC、DMA2D、GT911 触摸、QSPI Flash、FAL/SFUD、FinSH 和 LVGL 的基础适配，并运行 LVGL Music Demo。

## 硬件

- MCU：STM32H743XIH6，Arm Cortex-M7
- 系统主频：480 MHz
- 外部晶振：25 MHz
- MCU 内部 Flash：2 MiB
- 当前链接脚本使用的内部 Flash 空间：1 MiB，起始地址 `0x08000000`
- DTCM RAM：128 KiB，起始地址 `0x20000000`
- AXI SRAM：512 KiB，起始地址 `0x24000000`
- 外部 SDRAM：32 MiB，起始地址 `0xC0000000`，32-bit FMC 总线
- 显示：800 × 480(触摸范围1024 x 768)、RGB565、LTDC
- 图形加速：DMA2D
- 显示缓冲：SDRAM 双缓冲
  - Framebuffer 0：`0xC0000000`
  - Framebuffer 1：`0xC00BB800`
- 触摸控制器：GT911，I²C 软件总线
- 外部 Flash：W25Q64，QSPI + SFUD + FAL
- 调试串口：USART1，PA9/PA10
- 用户 LED：PB0
- LCD 背光：PB5

硬件购买页面：

- [闲鱼商品链接](https://www.goofish.com/item?spm=a21ybx.personal.feeds.1.4a996ac2xnbP3a&id=978667380425&categoryId=125952002)

> 闲鱼商品信息可能发生变更或需要登录才能查看。以上硬件参数以本工程当前驱动和链接脚本为准，使用其他批次或相似板卡前请核对原理图、SDRAM位宽、LCD时序和引脚定义。

## 软件组件

| 组件 | 版本/状态 | 用途 |
| --- | --- | --- |
| RT-Thread | 4.1.1 | 实时操作系统内核 |
| STM32H7 HAL/CMSIS | 工程内置 | 芯片启动和外设驱动 |
| LVGL | 8.3.11 | 图形界面 |
| FAL | RT-Thread 组件 | Flash 抽象层 |
| SFUD | RT-Thread 组件 | SPI/QSPI Flash 通用驱动 |
| Elm-FatFs | RT-Thread 组件 | FAT 文件系统 |
| DevFS | RT-Thread 组件 | 设备文件系统 |
| FinSH/MSH | 已启用 | 命令行 Shell |
| ULog | 已启用 | 日志系统 |
| Newlib | 已启用 | C 标准库 |
| POSIX/Pthreads | 已启用 | POSIX 延时、时钟和线程接口 |
| GT911 | 1.0.0 | 电容触摸驱动 |
| rt_vsnprintf_full | latest | 完整格式化输出支持 |

## 当前功能

- RT-Thread 内核、线程、IPC、软件定时器和堆管理
- FinSH/MSH 控制台
- STM32H743 480 MHz 时钟初始化
- 32 MiB SDRAM 初始化和 MPU/Cache 配置
- 800 × 480 LTDC RGB565 显示
- SDRAM 整帧双缓冲
- DMA2D 填充和拷贝加速
- LVGL 显示与输入设备端口
- LVGL Music Demo
- GT911 五点触摸及坐标映射
- QSPI W25Q64
- SFUD/FAL Flash 设备
- Elm-FatFs 和 DevFS
- USART、GPIO、I²C、SPI、QSPI、RTC 等 RT-Thread 设备框架
- PB0 LED 周期翻转

## 工程结构

```text
.
├── applications/                用户应用和 LVGL 端口
│   └── lvgl_port/               显示、触摸输入适配
├── drivers/                     STM32H743 BSP 和板级驱动
├── libraries/                   CMSIS 和 STM32H7 HAL
├── linkscripts/                 GNU ld 链接脚本
├── packages/                    LVGL、GT911 等软件包
├── rt-thread/                   RT-Thread 4.1.1 源码
├── cmake/                       CMake 工具链和组件配置
│   └── components/              分组件动态源码发现
├── CMakeLists.txt               CMake 工程入口
├── CMakePresets.json            Debug/Release 构建预设
├── rtconfig.h                   RT-Thread 功能配置
└── SConstruct                   RT-Thread/SCons 工程入口
```

## 使用 RT-Thread Studio 构建

1. 使用 RT-Thread Studio 打开本工程目录。
2. 右键工程并选择 **Refresh**。
3. 选择 **Project → Build Project**，或点击工具栏构建按钮。
4. 构建输出生成在 `Debug/` 目录。

工程的 `.cproject` 已排除 CMake 的 `build/` 目录。不要取消该排除项，否则 RT-Thread Studio 会把 `CMakeCCompilerId.c` 和 CMake 中间文件识别成工程源码，导致 `main` 重复定义或其他链接错误。

## 使用 CLion 构建

CLion 可以直接识别 `CMakePresets.json`。

1. 使用 CLion 打开工程根目录。
2. 等待 CMake 自动加载。
3. 在顶部 CMake Profile 中选择：
   - `rt-studio-debug`
   - `rt-studio-release`
4. 选择 `rtthread` 目标。
5. 点击锤子按钮或选择 **Build → Build Project**。

若预设没有自动加载，可执行 **Tools → CMake → Reset Cache and Reload Project**。

输出目录：

```text
build/cmake-debug/
├── rtthread.elf
├── rtthread.bin
└── rtthread.map

build/cmake-release/
├── rtthread.elf
├── rtthread.bin
└── rtthread.map
```

## 使用命令行构建

### Windows

本机默认优先使用 RT-Thread Studio 自带的 GNU Arm Embedded 5.4.1。需要安装或使用 CLion 内置的 CMake 与 Ninja。

Debug：

```powershell
cmake --preset rt-studio-debug
cmake --build --preset rt-studio-debug
```

Release：

```powershell
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

如果 GNU Arm Embedded 工具链不在默认位置：

```powershell
$env:ARM_GCC_ROOT = "D:\Tools\gcc-arm-none-eabi"
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

### Ubuntu

安装依赖：

```bash
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi
```

构建：

```bash
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

如果工具链不在 `PATH`：

```bash
export ARM_GCC_ROOT=/opt/gcc-arm-none-eabi-5_4-2016q3
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

为了尽量获得一致的二进制结果，Windows、Ubuntu 和 RT-Thread Studio 应使用相同版本的 GNU Arm Embedded 工具链。当前基准版本为 5.4.1。

## Debug 与 Release

| 配置 | 主要选项 | 调试信息 |
| --- | --- | --- |
| Debug | `-O0 -g -gdwarf-2` | 保留 |
| Release | `-O2 -Wl,--strip-debug` | 不保留 |

两个配置均使用：

```text
-mcpu=cortex-m7
-mthumb
-mfloat-abi=hard
-mfpu=fpv5-sp-d16
-ffunction-sections
-fdata-sections
```

## 添加和删除源码

CMake 使用 `CONFIGURE_DEPENDS` 动态发现源码：

- 在 `applications/` 中添加或删除 C/汇编文件会自动识别。
- 在 `drivers/` 中添加或删除 C/汇编文件会自动识别。
- 已注册的 RT-Thread、HAL、LVGL 和软件包目录也会自动识别文件变化。
- CLion/Ninja 构建时会自动重新检查目录并刷新 CMake。

新增一个全新的第三方组件目录时，需要在相应文件中注册目录：

```text
cmake/components/platform.cmake
cmake/components/rtthread.cmake
cmake/components/lvgl.cmake
cmake/components/packages.cmake
```

如果同一个目录中存在不应参与编译的源码，应将它加入对应组件文件的 `list(REMOVE_ITEM ...)` 排除清单。

## 下载与调试

CMake 当前负责生成 ELF、BIN 和 MAP 文件，不直接执行烧录。

- 使用 RT-Thread Studio 时，可通过 IDE 中已配置的调试器下载和调试。
- 使用 CLion 时，需要另行配置 OpenOCD、J-Link 或 STM32CubeProgrammer。
- 烧录前请确认目标地址与链接脚本一致，当前程序入口位于内部 Flash `0x08000000`。

## 配置注意事项

- RT-Thread 功能配置以 `rtconfig.h` 为准。
- 芯片、时钟、串口和外设引脚配置主要位于 `drivers/board.h`。
- SDRAM、LTDC、DMA2D、GT911 和 QSPI 初始化主要位于 `drivers/board.c`。
- CMake 链接脚本为 `linkscripts/STM32H743XIHx/link.lds`。
- 当前链接脚本只分配了 1 MiB 内部 Flash，虽然 STM32H743XIH6 具有 2 MiB Flash；扩展程序空间前应同步检查 Flash 布局、FAL 分区和升级方案。
- LVGL 使用两个完整的 800 × 480 RGB565 帧缓冲，每个缓冲区占用 768,000 字节。
- 修改 `rtconfig.h`、启用新的 RT-Thread 组件或安装新软件包后，应同步检查 `cmake/components/` 中的组件目录和排除项。

## 许可证

RT-Thread、LVGL、STM32 HAL/CMSIS 及各软件包分别遵循其自身目录中的许可证。新增或分发代码前请检查对应组件的许可证文件与版权声明。
