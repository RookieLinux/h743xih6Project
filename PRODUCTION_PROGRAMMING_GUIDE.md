# 字库、城市列表与 APP 固件制作及批量烧录教程

[中文](PRODUCTION_PROGRAMMING_GUIDE.md) | [English](PRODUCTION_PROGRAMMING_GUIDE_EN.md)

本文面向本工程的发布和量产操作，覆盖以下内容：

- 制作 QSPI 中文字库和城市列表；
- 制作可批量烧录的 QSPI FAT 文件系统镜像；
- 生成带 ECDSA P-256 签名的 APP 固件包；
- 将 APP 固件部署到 Caddy 文件服务器；
- 通过 STM32CubeProgrammer CLI 按 ST-LINK 序列号批量烧录资源和 APP。

文中命令默认在 `h743xih6Project` 工程根目录执行。量产前必须先用一台样机验证所用外部加载器、地址和镜像，再扩大批次。

## 1. Flash 布局和产物

### 1.1 内部 Flash

| 内容 | 地址 | 说明 |
| --- | ---: | --- |
| Bootloader | `0x08000000` | Bootloader 实际限制在首个 128 KiB 扇区，Bank 1 其余空间保留 |
| APP | `0x08100000` | APP 最大 1 MiB，BIN 必须链接到该地址 |

### 1.2 W25Q64 QSPI

下表的“烧录地址”假定 QSPI 外部加载器把 W25Q64 映射到 `0x90000000`。如果所用 `.stldr` 使用其他基地址，必须按加载器定义换算，不能照抄地址。

| 分区 | QSPI 偏移 | 常用烧录地址 | 大小 | 用途 |
| --- | ---: | ---: | ---: | --- |
| `upgrade` | `0x000000` | `0x90000000` | 1 MiB | 下次启动时由 Bootloader 验签并安装 |
| `factory` | `0x100000` | `0x90100000` | 1 MiB | APP 损坏时的持久恢复包 |
| `download` | `0x200000` | `0x90200000` | 1 MiB | APP 的 OTA 下载暂存区，不预烧录 |
| `filesystem` | `0x300000` | `0x90300000` | 5 MiB | FAT 文件系统，保存字库和城市列表 |

发布时通常得到以下四个产物：

| 产物 | 用途 |
| --- | --- |
| `rtthread.bin` | 直接烧录内部 APP 区，仅用于开发或明确选择的直烧流程 |
| `h743_Vx.y.z.fwpkg` | 紧凑的签名 OTA 包，部署到文件服务器 |
| `h743_Vx.y.z_factory_1MiB.fwpkg` | 填充到完整 1 MiB 的签名包，批量烧录 `upgrade`/`factory` |
| `lvww_filesystem_5MiB.bin` | 包含 `/ui` 目录的完整 FAT 镜像，批量烧录 `filesystem` |

工程内的 `tools/mkimage.py` 是从配套 Bootloader 同步保存的打包脚本。固件包格式、分区尺寸或签名字段发生变化时，APP 和 Bootloader 两个工程中的脚本及 `boot_image.h` 必须一起更新。

## 2. 制作字库和城市列表

### 2.1 准备环境

主机需要 Python 3 和 Pillow：

```powershell
python -m pip install Pillow
```

首次生成时，脚本会从 Adobe Source Han Sans 官方 Release 下载约 90.8 MiB 的字体压缩包并校验 SHA-256，之后会复用 `applications/lv_wifi_weather/tools/.cache/` 中的缓存。

### 2.2 更新城市源数据（可选）

如果需要更新城市列表，用 QWeather LocationList 的最新版 `China-City-List-latest.csv` 替换：

```text
applications/lv_wifi_weather/tools/China-City-List-latest.csv
```

源文件和生成文件都使用 UTF-8；不要用会自动改列名、日期或编码的表格软件另存 CSV。

### 2.3 一键生成

```powershell
python .\applications\lv_wifi_weather\tools\generate_all_resources.py
```

该命令会同时更新：

- 固件内置的小字库 `applications/lv_wifi_weather/src/lvww_font_cjk_16.c`；
- QSPI 字库 `resources/qspi/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt`；
- 城市列表 `resources/qspi/ui/data/cities_zh.tsv`；
- 字符表、字体许可证和 `manifest.json`。

完整输出目录为：

```text
applications/lv_wifi_weather/resources/qspi/ui/
├── data/cities_zh.tsv
├── fonts/lvww_source_han_sans_sc_16_4bpp.fnt
├── fonts/LICENSE-SourceHanSans.txt
├── images/
├── text/common_chars_utf8.txt
└── manifest.json
```

生成后检查脚本没有报错，并确认 `manifest.json` 中的版本、记录数、文件大小和 SHA-256 符合本次发布预期。当前字库约 2.9 MiB，城市列表约 346 KiB；实际值以清单为准。

## 3. 制作 5 MiB QSPI FAT 镜像

字库和城市列表是 FAT 文件系统中的普通文件，运行时路径分别为：

```text
/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
/ui/data/cities_zh.tsv
```

**不能把 `.fnt` 或 `.tsv` 单独写到某个裸 Flash 偏移。** 批量生产时应先制作完整的 5 MiB FAT 镜像，再把镜像整体写入 `filesystem` 分区。

本工程的 FAL 块设备逻辑扇区为 4096 字节。因此离线制作 FAT 镜像时也必须使用 4096 字节扇区；用常见的 512 字节默认值制作的镜像无法被本工程正确挂载。

### 3.1 Ubuntu 或 WSL 离线制作（推荐）

安装工具：

```bash
sudo apt update
sudo apt install -y dosfstools mtools
```

进入工程根目录后执行：

```bash
mkdir -p build/production
truncate -s 5MiB build/production/lvww_filesystem_5MiB.bin
mkfs.fat -F 12 -S 4096 -s 1 -n LVWW_UI build/production/lvww_filesystem_5MiB.bin
mcopy -s -i build/production/lvww_filesystem_5MiB.bin \
  applications/lv_wifi_weather/resources/qspi/ui ::/
fsck.fat -vn build/production/lvww_filesystem_5MiB.bin
mdir -s -i build/production/lvww_filesystem_5MiB.bin ::/ui
```

检查要点：

- 镜像文件大小必须恰好为 `5242880` 字节；
- `mdir` 必须能看到 `/ui/fonts/...fnt` 和 `/ui/data/cities_zh.tsv`；
- 路径必须是 `/ui/...`，不能多出一层 `qspi` 或第二层 `ui`；
- `fsck.fat` 不应报告文件系统错误。

PowerShell 可检查镜像大小和哈希：

```powershell
Get-Item .\build\production\lvww_filesystem_5MiB.bin |
  Select-Object Name, Length
Get-FileHash .\build\production\lvww_filesystem_5MiB.bin -Algorithm SHA256
```

### 3.2 黄金样机回读法（备用）

如果主机 FAT 工具无法生成 4096 字节扇区镜像，可以让一台样机先由本工程自动格式化 `filesystem`，再通过 YMODEM 把整个 `qspi/ui` 目录内容传入对应的 `/ui` 路径。确认字库和城市搜索均正常后，使用已验证的板级 QSPI 外部加载器，从 `0x90300000` 回读 `0x00500000` 字节作为黄金镜像。

STM32CubeProgrammer CLI 的典型回读形式如下，具体参数以所安装版本的帮助为准：

```powershell
& $Programmer -c "port=SWD sn=$Serial" -el $Loader `
  -u .\build\production\lvww_filesystem_5MiB.bin 0x90300000 0x00500000
```

回读后必须再次烧到另一台空白样机验证，不能仅凭文件大小判定镜像可用。

## 4. 构建 APP 并生成签名包

### 4.1 统一版本号

发布前更新 `applications/ota/ota_config.h` 中的：

```c
#define OTA_CURRENT_FIRMWARE_VERSION_CODE  0x00010004UL
#define OTA_CURRENT_FIRMWARE_VERSION_TEXT  "V1.0.4"
```

`VERSION_CODE` 必须单调递增，并与后续 `mkimage.py --version`、OTA 响应器的 `--version-code` 完全一致。显示字符串 `V1.0.4` 只用于界面显示，设备实际按数值版本比较。

### 4.2 构建 Release APP

使用 RT-Thread Studio 的 Release 配置构建后，通常得到：

```text
Release/rtthread.bin
```

也可以使用 CMake：

```powershell
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

CMake 输出为：

```text
build/cmake-release/rtthread.bin
```

确认构建成功，并且 BIN 不超过 `1044480` 字节（1 MiB 固件槽减去 4 KiB 包头扇区）。`mkimage.py` 还会检查向量表是否链接到 `0x08100000`。

### 4.3 准备签名密钥

打包签名依赖 `cryptography`：

```powershell
python -m pip install cryptography
```

首次开发可生成 P-256 密钥：

```powershell
python .\tools\ota_signing_key.py
```

默认私钥位于 `tools/keys/ota_signing_private.pem`，该路径已被 Git 忽略。脚本同时生成 APP 公钥头 `applications/ota/ota_trusted_key.h`；必须把同一个公钥头同步到 Bootloader 的 `bootloader/ota_trusted_key.h`，然后重新构建 Bootloader。

量产注意事项：

- 私钥不能提交 Git，也不能复制到文件服务器或普通烧录电脑；
- 生产密钥应在离线签名机或 HSM 中管理；
- 不要日常使用 `--force-new-key`，否则已部署设备将不再信任新签名；
- APP 和 Bootloader 内置公钥指纹必须一致；
- `--unsigned` 只允许用于旧设备的一次性迁移，禁止用于常规发布。

### 4.4 生成 OTA 包和量产槽镜像

以下示例使用 CMake Release 输出和版本 `V1.0.4 / 0x00010004`：

```powershell
New-Item -ItemType Directory -Force .\build\production

python .\tools\mkimage.py `
  .\build\cmake-release\rtthread.bin `
  .\build\production\h743_V1.0.4.fwpkg `
  --version 0x00010004 `
  --signing-key .\tools\keys\ota_signing_private.pem

python .\tools\mkimage.py `
  .\build\cmake-release\rtthread.bin `
  .\build\production\h743_V1.0.4_factory_1MiB.fwpkg `
  --version 0x00010004 `
  --signing-key .\tools\keys\ota_signing_private.pem `
  --pad-slot
```

如果使用 RT-Thread Studio 输出，只需把输入路径换成 `Release/rtthread.bin`。

第一份包不填充，适合 HTTP 下载；第二份包用 `0xFF` 填充到完整 1 MiB，适合烧录器完整擦写槽位。两者都包含相同版本和 APP 摘要的有效签名，但由于 ECDSA 签名过程允许使用随机数，两个文件的签名字节和文件哈希不要求相同。

记录发布哈希：

```powershell
Get-FileHash .\build\production\h743_V1.0.4*.fwpkg -Algorithm SHA256
```

## 5. 将签名 APP 包部署到文件服务器

服务器环境的完整搭建方法见 [SERVER_ENVIRONMENT_SETUP.md](SERVER_ENVIRONMENT_SETUP.md)。这里只说明每次发布固件时的操作。

### 5.1 Windows Caddy

先复制为临时文件，校验后再原子改名，避免设备下载到半个文件：

```powershell
$Source = ".\build\production\h743_V1.0.4.fwpkg"
$TargetDir = ".\server\www\firmware\h743"
$Target = Join-Path $TargetDir "V1.0.4.fwpkg"
$Temporary = "$Target.tmp"

New-Item -ItemType Directory -Force $TargetDir
Copy-Item -LiteralPath $Source -Destination $Temporary -Force
Move-Item -LiteralPath $Temporary -Destination $Target -Force
Get-FileHash $Source, $Target -Algorithm SHA256
```

目标下载地址为：

```text
http://<服务器IP>:8000/firmware/h743/V1.0.4.fwpkg
```

仅替换静态文件不需要重启 Caddy。推荐每个版本使用不可变的新文件名，不要在设备下载期间覆盖同名文件。

### 5.2 Ubuntu Caddy

```bash
sudo install -d -o caddy -g caddy /srv/h743xih6Project/www/firmware/h743
sudo install -o caddy -g caddy -m 0644 \
  build/production/h743_V1.0.4.fwpkg \
  /srv/h743xih6Project/www/firmware/h743/V1.0.4.fwpkg.tmp
sudo mv -f \
  /srv/h743xih6Project/www/firmware/h743/V1.0.4.fwpkg.tmp \
  /srv/h743xih6Project/www/firmware/h743/V1.0.4.fwpkg
sha256sum \
  build/production/h743_V1.0.4.fwpkg \
  /srv/h743xih6Project/www/firmware/h743/V1.0.4.fwpkg
```

### 5.3 下载和 MQTT 元数据验证

先从设备可访问的网络验证 URL：

```powershell
curl.exe -I http://<服务器IP>:8000/firmware/h743/V1.0.4.fwpkg
```

本工程的测试响应器会下载服务器文件并与本地 `--package` 逐字节比较，然后自动计算包大小、CRC32 和 SHA-256：

```powershell
python .\tools\ota_mqtt_responder.py `
  --package .\build\production\h743_V1.0.4.fwpkg `
  --url http://<服务器IP>:8000/firmware/h743/V1.0.4.fwpkg `
  --version V1.0.4 `
  --version-code 0x00010004
```

出现 `HTTP package check: OK` 后再开始设备升级。不要使用 `--skip-http-check` 进行正式发布。

## 6. 使用 STM32CubeProgrammer 批量烧录

### 6.1 前置条件

准备以下工具和文件：

- `STM32_Programmer_CLI.exe`；
- 与本板 QSPI 引脚、时序和 W25Q64 型号匹配并经过验证的 `.stldr` 外部加载器；
- Bootloader BIN（量产整机时需要）；
- `h743_Vx.y.z_factory_1MiB.fwpkg`；
- `lvww_filesystem_5MiB.bin`；
- 每个工位 ST-LINK 的序列号。

本仓库不包含板级 `.stldr`，不能随意选择其他开发板同为 W25Q64 的加载器。Flash 型号相同并不代表 QSPI 引脚和初始化参数相同。

先用以下命令确认 CLI 可以看到探针：

```powershell
& $Programmer -l
```

建议连接参数使用硬件复位和 Connect Under Reset；若板卡实际复位线路不同，应在样机验证后调整。

### 6.2 推荐的签名量产流程

推荐将完整签名包同时写入 `upgrade` 和 `factory`：

1. Bootloader 写入内部 Flash；
2. 签名包写入 `upgrade`，第一次启动时由 Bootloader 验签并安装到内部 APP 区；
3. 同一签名包写入 `factory`，用于以后恢复；
4. 资源 FAT 镜像写入 `filesystem`；
5. 复位，检查 Bootloader 日志、APP 版本、字库和城市搜索。

这样首次安装和恢复都经过 Bootloader 的签名校验。`download` 分区保持擦除状态。

下面的 PowerShell 示例按 ST-LINK 序列号逐台顺序烧录。先填写真实路径和序列号：

```powershell
$Programmer = "C:\Program Files\STMicroelectronics\STM32Cube\STM32CubeProgrammer\bin\STM32_Programmer_CLI.exe"
$Loader = "D:\programming\H743_W25Q64.stldr"
$Bootloader = "D:\firmware\bootloader.bin"
$SignedSlot = ".\build\production\h743_V1.0.4_factory_1MiB.fwpkg"
$Filesystem = ".\build\production\lvww_filesystem_5MiB.bin"
$ProbeSerials = @(
  "STLINK_SERIAL_1",
  "STLINK_SERIAL_2"
)

foreach ($Serial in $ProbeSerials) {
  $Connection = @("port=SWD", "sn=$Serial", "mode=UR", "reset=HWrst")
  Write-Host "Programming $Serial ..."

  & $Programmer -c $Connection -d $Bootloader 0x08000000 -v
  if ($LASTEXITCODE -ne 0) { throw "Bootloader failed: $Serial" }

  & $Programmer -c $Connection -el $Loader -d $SignedSlot 0x90000000 -v
  if ($LASTEXITCODE -ne 0) { throw "upgrade failed: $Serial" }

  & $Programmer -c $Connection -el $Loader -d $SignedSlot 0x90100000 -v
  if ($LASTEXITCODE -ne 0) { throw "factory failed: $Serial" }

  & $Programmer -c $Connection -el $Loader -d $Filesystem 0x90300000 -v -rst
  if ($LASTEXITCODE -ne 0) { throw "filesystem failed: $Serial" }

  Write-Host "PASS: $Serial"
}
```

不同 STM32CubeProgrammer 版本对 `-el` 接受完整路径还是已安装加载器名称可能有差异，正式执行前用 `STM32_Programmer_CLI.exe --help` 和一台样机确认。任何一步失败时脚本会停止，失败板不得混入合格品。

### 6.3 直接烧录内部 APP（开发/返修可选）

也可以把 `rtthread.bin` 直接写到 `0x08100000`：

```powershell
$App = ".\build\cmake-release\rtthread.bin"

foreach ($Serial in $ProbeSerials) {
  $Connection = @("port=SWD", "sn=$Serial", "mode=UR", "reset=HWrst")
  & $Programmer -c $Connection -d $App 0x08100000 -v -rst
  if ($LASTEXITCODE -ne 0) { throw "APP failed: $Serial" }
}
```

`rtthread.bin` 是裸 APP，不包含 `.fwpkg` 包头和 ECDSA 签名。直接烧录会绕过 Bootloader 的包验签，所以量产若要求签名链路，应使用上一节的“签名包写 `upgrade` 后由 Bootloader 安装”流程。即使选择直烧 APP，也建议把有效签名包写入 `factory` 作为恢复镜像。

### 6.4 烧录后抽检

每批至少抽检以下项目：

- UART1 Bootloader 日志没有包头、CRC、SHA-256 或签名错误；
- APP 显示的版本与本批工单一致；
- `ls -l /ui/fonts` 中字库大小与 `manifest.json` 一致；
- `ls -l /ui/data` 中存在 `cities_zh.tsv`；
- `uires_reload` 成功，中文显示正常；
- 用中文、英文或拼音搜索至少两个非默认城市；
- 触发一次 factory 恢复或 OTA 升级抽检，确认公钥和签名链路一致；
- 保存本批 Bootloader、APP、签名包、资源镜像的 SHA-256 及烧录器日志。

## 7. 常见错误

| 现象 | 常见原因 | 处理 |
| --- | --- | --- |
| APP 启动后又格式化 QSPI | FAT 镜像扇区大小不是 4096、镜像损坏或写错地址 | 重做 5 MiB 镜像并检查外部加载器基地址 |
| 找不到字库或城市列表 | 镜像内目录多/少一层，未形成 `/ui/...` | 用 `mdir -s` 检查镜像内部路径 |
| `mkimage.py` 报向量表无效 | 使用了错误 BIN，或 APP 未链接到 `0x08100000` | 检查链接脚本和构建输出 |
| Bootloader 报签名失败 | 打包私钥与 Bootloader/APP 公钥不匹配 | 对比两个工程公钥头的指纹，重新构建并签名 |
| 设备提示无更新 | `version_code` 未递增，或 MQTT 响应版本与包头版本不一致 | 统一配置、打包和响应器的数值版本 |
| HTTP 校验失败 | URL 指向旧文件、复制未完成或 Caddy 根目录不一致 | 比较本地与服务器 SHA-256，使用临时文件原子改名 |
| 外部 Flash 烧录失败 | `.stldr` 与本板 QSPI 引脚/时序不匹配 | 停止批量烧录，先验证或制作板级外部加载器 |
