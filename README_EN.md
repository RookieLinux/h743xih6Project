# STM32H743XIH6 RT-Thread + LVGL Project

[中文](README.md) | [English](README_EN.md)

This repository contains the RT-Thread application firmware for an STM32H743XIH6 board. It can be developed and built with RT-Thread Studio or CMake/CLion.

The project provides board support for an 800 × 480 RGB LCD, SDRAM framebuffers, LTDC, DMA2D, GT911 touch input, QSPI Flash, FAL/SFUD, FinSH, and LVGL. The default application is a Chinese Wi-Fi weather UI powered by the RW007 module. It supports wireless configuration, local city search, Open-Meteo weather requests, offline caching, and a QSPI-backed streaming Chinese font.

> This repository contains the application only. The matching bootloader is maintained in [RookieLinux/h743xih6Bootloader](https://github.com/RookieLinux/h743xih6Bootloader).

## Firmware layout

The STM32H743XIH6's 2 MiB internal Flash is divided between the bootloader and application:

| Firmware | Address range | Size | Repository |
| --- | --- | --- | --- |
| Bootloader | `0x08000000`–`0x080FFFFF` | 1 MiB | [RookieLinux/h743xih6Bootloader](https://github.com/RookieLinux/h743xih6Bootloader) |
| Application | `0x08100000`–`0x081FFFFF` | 1 MiB | This repository |

The application link address and vector table both start at `0x08100000`. After reset, the bootloader validates and starts the application. Early application initialization sets `SCB->VTOR` to the application vector table. When updating, flashing, or debugging the firmware, make sure the BIN/ELF target address matches this layout.

## Hardware

- MCU: STM32H743XIH6, Arm Cortex-M7
- System clock: 480 MHz
- External oscillator: 25 MHz
- Internal Flash: 2 MiB
- Application Flash: 1 MiB at `0x08100000`
- DTCM RAM: 128 KiB at `0x20000000`
- AXI SRAM: 512 KiB at `0x24000000`
- External SDRAM: 32 MiB at `0xC0000000`, 32-bit FMC bus
- Display: 800 × 480, RGB565, LTDC
- Raw touch range: 1024 × 768
- Graphics acceleration: DMA2D
- Display buffers: two full-frame buffers in SDRAM
  - Framebuffer 0: `0xC0000000`
  - Framebuffer 1: `0xC00BB800`
- Touch controller: GT911 on a software I²C bus
- External Flash: W25Q64, QSPI + SFUD + FAL
- Debug console: USART1 on PA9/PA10
- User LED: PB0
- LCD backlight: PB5

Hardware purchase page:

- [Goofish product listing](https://www.goofish.com/item?spm=a21ybx.personal.feeds.1.4a996ac2xnbP3a&id=978667380425&categoryId=125952002)

> The listing may change or require an account. Treat the drivers and linker script in this project as authoritative, and verify the schematic, SDRAM width, LCD timing, and pinout before using another board revision.

## Software

| Component | Version/status | Purpose |
| --- | --- | --- |
| RT-Thread | 4.1.1 | Real-time operating system |
| STM32H7 HAL/CMSIS | Included | Startup and peripheral support |
| LVGL | 8.3.11 | Graphical user interface |
| FAL | RT-Thread component | Flash abstraction layer |
| SFUD | RT-Thread component | Generic SPI/QSPI Flash driver |
| Elm-FatFs | RT-Thread component | FAT filesystem |
| DevFS | RT-Thread component | Device filesystem |
| FinSH/MSH | Enabled | Command shell |
| ULog | Enabled | Logging |
| Newlib | Enabled | C standard library |
| POSIX/Pthreads | Enabled | POSIX delay, clock, and thread APIs |
| GT911 | 1.0.0 | Capacitive touch driver |
| RW007 | 2.1.0 | SPI Wi-Fi module and WLAN support |
| rt_vsnprintf_full | latest | Full formatted-output support |
| Source Han Sans SC | 2.005R | Embedded and QSPI Chinese font source |

## Features

- RT-Thread kernel, threads, IPC, software timers, and heap management
- FinSH/MSH console
- STM32H743 clock initialization at 480 MHz
- 32 MiB SDRAM initialization and MPU/cache configuration
- 800 × 480 LTDC RGB565 display
- Two full-frame SDRAM buffers
- DMA2D fill and copy acceleration
- LVGL display and input ports
- Chinese Wi-Fi weather UI with home, wireless, city, and time-zone screens
- RW007 scanning, credential storage, connect/disconnect, and status display
- Open-Meteo weather data and network time synchronization
- Built-in default city and offline Chinese city search from QSPI
- Source Han Sans SC 16 px, 4bpp antialiased Chinese font
- On-demand QSPI font reads with an eight-glyph cache
- Runtime font unload, replacement, and reload
- GT911 five-point touch and coordinate mapping
- W25Q64 QSPI Flash with SFUD/FAL
- Elm-FatFs and DevFS
- RT-Thread USART, GPIO, I²C, SPI, QSPI, and RTC device support
- Periodic PB0 LED toggle

## Repository structure

```text
.
├── applications/                User applications and LVGL ports
│   ├── lvgl_port/               Display and touch input adaptation
│   └── lv_wifi_weather/         Weather UI, RW007 backend, and QSPI tools
├── drivers/                     STM32H743 BSP and board drivers
├── libraries/                   CMSIS and STM32H7 HAL
├── linkscripts/                 GNU ld linker scripts
├── packages/                    LVGL, GT911, and other packages
├── rt-thread/                   RT-Thread 4.1.1 source
├── cmake/                       CMake toolchain and component configuration
│   └── components/              Component-based source discovery
├── CMakeLists.txt               CMake entry point
├── CMakePresets.json            Debug and Release presets
├── rtconfig.h                   RT-Thread feature configuration
└── SConstruct                   RT-Thread/SCons entry point
```

## Building with RT-Thread Studio

1. Open the project directory in RT-Thread Studio.
2. Right-click the project and select **Refresh**.
3. Select **Project → Build Project**, or click the build button.
4. Build artifacts are written to `Debug/`.

The `.cproject` file excludes CMake's `build/` directory. Keep this exclusion; otherwise RT-Thread Studio may treat `CMakeCCompilerId.c` and other generated files as project sources, resulting in duplicate `main` definitions or linker errors.

## Building with CLion

CLion can load `CMakePresets.json` directly:

1. Open the repository root in CLion.
2. Wait for CMake to load.
3. Select either `rt-studio-debug` or `rt-studio-release` as the CMake profile.
4. Select the `rtthread` target.
5. Build the project.

If the presets are not loaded automatically, use **Tools → CMake → Reset Cache and Reload Project**.

Build outputs:

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

## Command-line build

### Windows

The default setup uses GNU Arm Embedded 5.4.1 bundled with RT-Thread Studio. CMake and Ninja are also required.

```powershell
# Debug
cmake --preset rt-studio-debug
cmake --build --preset rt-studio-debug

# Release
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

Set `ARM_GCC_ROOT` if the toolchain is installed elsewhere:

```powershell
$env:ARM_GCC_ROOT = "D:\Tools\gcc-arm-none-eabi"
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

### Ubuntu

```bash
sudo apt update
sudo apt install cmake ninja-build gcc-arm-none-eabi

cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

If the toolchain is not in `PATH`:

```bash
export ARM_GCC_ROOT=/opt/gcc-arm-none-eabi-5_4-2016q3
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

Use the same GNU Arm Embedded version on Windows, Ubuntu, and RT-Thread Studio when reproducible binaries are important. The current baseline is version 5.4.1.

## Weather application and QSPI resources

The weather application lives in `applications/lv_wifi_weather/`. The default city is built into the firmware, while other Chinese cities are searched from an offline database on the QSPI FAT filesystem. City search works offline; weather retrieval and time synchronization require Wi-Fi.

Runtime resource layout:

```text
/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
/ui/fonts/LICENSE-SourceHanSans.txt
/ui/data/cities_zh.tsv
/ui/text/common_chars_utf8.txt
/ui/images/
/ui/manifest.json
```

- The Chinese font is generated from [Adobe Source Han Sans](https://github.com/adobe-fonts/source-han-sans) Simplified Chinese Regular, pinned to version `2.005R`.
- City data is generated from the `China-City-List-latest.csv` file in [QWeather LocationList](https://github.com/qwd/LocationList), currently producing 3,577 records.
- Text, city data, and manifests use UTF-8 without a BOM. The `.fnt` file is a project-specific little-endian binary format with 4bpp glyph alpha.
- Font download information and SHA-256 hashes for inputs and outputs are recorded in `manifest.json`.

Python 3 and Pillow are required on the host. The first run downloads and verifies about 90.8 MiB from the official Adobe GitHub release; later runs reuse `tools/.cache/`.

Regenerate the embedded font, QSPI font, city database, license, and resource manifest with:

```powershell
python applications/lv_wifi_weather/tools/generate_all_resources.py
```

Copy the generated `applications/lv_wifi_weather/resources/qspi/ui/` tree as regular files to `/ui` on the board's FAT filesystem. Do not flash the `.fnt` file directly to a raw Flash offset.

### Updating the font over the serial console

The external font remains open while it is in use. Release it before replacing the file:

```text
uires_unload
rm /ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
ry
mv /lvww_source_han_sans_sc_16_4bpp.fnt /ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
uires_reload
```

After the YMODEM transfer, use `ls -l` to verify that the `.fnt` file is `2913410` bytes. The `filesystem` partition is 5 MiB and cannot hold two complete copies at once, so delete the old font before transferring the replacement.

Useful resource commands:

| Command | Purpose |
| --- | --- |
| `uires_unload` | Fall back to the embedded font, close the external font, and release its index |
| `uires_reload` | Validate and reload the external font from `/ui/fonts/` |
| `ls -l /ui/fonts` | Check the font file name and size |
| `df` | Check free space on the QSPI FAT filesystem |

More details:

- `applications/lv_wifi_weather/README.md`
- `applications/lv_wifi_weather/resources/README.md`

## Debug and Release

| Configuration | Main options | Debug information |
| --- | --- | --- |
| Debug | `-O0 -g -gdwarf-2` | Included |
| Release | `-O2 -Wl,--strip-debug` | Stripped |

Both configurations use:

```text
-mcpu=cortex-m7
-mthumb
-mfloat-abi=hard
-mfpu=fpv5-sp-d16
-ffunction-sections
-fdata-sections
```

## Adding and removing source files

CMake uses `CONFIGURE_DEPENDS` to discover source files dynamically:

- C and assembly files added to or removed from `applications/` and `drivers/` are detected automatically.
- Registered RT-Thread, HAL, LVGL, and package directories are also monitored.
- CLion/Ninja rechecks the directories and refreshes CMake during builds.

Register a completely new third-party component directory in the relevant file:

```text
cmake/components/platform.cmake
cmake/components/rtthread.cmake
cmake/components/lvgl.cmake
cmake/components/packages.cmake
```

If a registered directory contains source files that must not be compiled, add them to the corresponding `list(REMOVE_ITEM ...)` exclusion list.

## Flashing and debugging

CMake generates ELF, BIN, and MAP files but does not flash the target.

- RT-Thread Studio can download and debug through its configured probe.
- CLion requires a separate OpenOCD, J-Link, or STM32CubeProgrammer setup.
- Flash the application BIN to internal Flash address `0x08100000`. ELF-aware tools obtain the address from the ELF file.
- For the complete boot flow, flash the matching [bootloader](https://github.com/RookieLinux/h743xih6Bootloader) to `0x08000000`.
- When debugging the application alone, start at the application's reset entry and use the vector table at `0x08100000`.

## Configuration notes

- `rtconfig.h` is the authoritative RT-Thread feature configuration.
- MCU, clock, console, and peripheral pin settings are mainly in `drivers/board.h`.
- SDRAM, LTDC, DMA2D, GT911, and QSPI initialization is mainly in `drivers/board.c`.
- The CMake linker script is `linkscripts/STM32H743XIHx/link.lds`.
- The application linker script assigns the 1 MiB Bank 2 range at `0x08100000`–`0x081FFFFF`; Bank 1 is reserved for the bootloader. Coordinate changes with the bootloader, Flash layout, FAL partitions, and update design.
- The W25Q64 `filesystem` FAL partition is mounted at `/`, has a size of 5 MiB, and starts at offset `0x00300000`.
- LVGL uses two complete 800 × 480 RGB565 framebuffers of 768,000 bytes each.
- After changing `rtconfig.h`, enabling an RT-Thread component, or installing a package, also review the component paths and exclusions in `cmake/components/`.

## License

RT-Thread, LVGL, STM32 HAL/CMSIS, and the included packages are governed by the licenses in their respective directories. Source Han Sans is licensed under the SIL Open Font License 1.1; its license is stored with the QSPI font resources at `applications/lv_wifi_weather/resources/qspi/ui/fonts/LICENSE-SourceHanSans.txt`. Review the applicable license and copyright notices before adding or distributing code.
