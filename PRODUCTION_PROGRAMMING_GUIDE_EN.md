# Font, City List, and APP Firmware Production Guide

[中文](PRODUCTION_PROGRAMMING_GUIDE.md) | [English](PRODUCTION_PROGRAMMING_GUIDE_EN.md)

This guide covers the release and production procedures for this project:

- generating the QSPI Chinese font and city list;
- building a QSPI FAT filesystem image for batch programming;
- creating an ECDSA P-256-signed APP package;
- deploying the APP package to a Caddy file server;
- batch-programming resources and the APP by ST-LINK serial number with STM32CubeProgrammer CLI.

Unless stated otherwise, run all commands from the `h743xih6Project` root directory. Before starting a production batch, always validate the external loader, addresses, and images on one sample board.

## 1. Flash layout and artifacts

### 1.1 Internal Flash

| Content | Address | Description |
| --- | ---: | --- |
| Bootloader | `0x08000000` | The Bootloader itself is limited to the first 128 KiB sector; the rest of Bank 1 is reserved |
| APP | `0x08100000` | Maximum size is 1 MiB; the BIN must be linked to this address |

### 1.2 W25Q64 QSPI

The programming addresses below assume that the QSPI external loader maps the W25Q64 at `0x90000000`. If the selected `.stldr` uses another base address, recalculate every address from that loader's definition.

| Partition | QSPI offset | Typical programming address | Size | Purpose |
| --- | ---: | ---: | ---: | --- |
| `upgrade` | `0x000000` | `0x90000000` | 1 MiB | Verified and installed by the Bootloader on the next boot |
| `factory` | `0x100000` | `0x90100000` | 1 MiB | Persistent recovery package used when the APP is invalid |
| `download` | `0x200000` | `0x90200000` | 1 MiB | APP OTA download staging area; do not preprogram it |
| `filesystem` | `0x300000` | `0x90300000` | 5 MiB | FAT filesystem containing the font and city list |

A release normally produces four artifacts:

| Artifact | Purpose |
| --- | --- |
| `rtthread.bin` | Direct programming of the internal APP area, for development or an explicitly selected direct-flash process |
| `h743_Vx.y.z.fwpkg` | Compact signed OTA package deployed to the file server |
| `h743_Vx.y.z_factory_1MiB.fwpkg` | Signed package padded to a complete 1 MiB slot for batch programming of `upgrade`/`factory` |
| `lvww_filesystem_5MiB.bin` | Complete FAT image containing `/ui`, programmed into `filesystem` |

The APP repository keeps its copy of the companion Bootloader packaging tool at `tools/mkimage.py`. If the package format, partition sizes, or signature fields change, update the scripts and `boot_image.h` in both the APP and Bootloader repositories together.

## 2. Generate the font and city list

### 2.1 Prepare the environment

Python 3 and Pillow are required:

```powershell
python -m pip install Pillow
```

On the first run, the generator downloads the approximately 90.8 MiB Source Han Sans archive from the official Adobe release and verifies its SHA-256. Later runs reuse the cache under `applications/lv_wifi_weather/tools/.cache/`.

### 2.2 Update the city source data (optional)

To update the city list, replace the following file with the latest QWeather LocationList `China-City-List-latest.csv`:

```text
applications/lv_wifi_weather/tools/China-City-List-latest.csv
```

The source and generated files use UTF-8. Do not resave the CSV using spreadsheet software that may silently alter column names, dates, or encoding.

### 2.3 Generate all resources

```powershell
python .\applications\lv_wifi_weather\tools\generate_all_resources.py
```

This command updates all of the following:

- the small firmware-resident font at `applications/lv_wifi_weather/src/lvww_font_cjk_16.c`;
- the QSPI font at `resources/qspi/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt`;
- the city list at `resources/qspi/ui/data/cities_zh.tsv`;
- the character list, font license, and `manifest.json`.

The complete output directory is:

```text
applications/lv_wifi_weather/resources/qspi/ui/
├── data/cities_zh.tsv
├── fonts/lvww_source_han_sans_sc_16_4bpp.fnt
├── fonts/LICENSE-SourceHanSans.txt
├── images/
├── text/common_chars_utf8.txt
└── manifest.json
```

Confirm that the command completes without errors. Check the source version, record count, file sizes, and SHA-256 values in `manifest.json` against the release requirements. The current font is approximately 2.9 MiB and the city list approximately 346 KiB; use the manifest as the authoritative source.

## 3. Build the 5 MiB QSPI FAT image

The font and city list are normal files inside the FAT filesystem. Their runtime paths are:

```text
/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
/ui/data/cities_zh.tsv
```

**Do not program the `.fnt` or `.tsv` file directly to an arbitrary raw Flash offset.** For production, first create a complete 5 MiB FAT image, then program that image into the `filesystem` partition.

The FAL block device in this project uses 4096-byte logical sectors. An offline FAT image must therefore also use 4096-byte sectors. An image created with the common 512-byte default cannot be mounted correctly by this firmware.

### 3.1 Offline generation on Ubuntu or WSL (recommended)

Install the required tools:

```bash
sudo apt update
sudo apt install -y dosfstools mtools
```

From the project root, run:

```bash
mkdir -p build/production
truncate -s 5MiB build/production/lvww_filesystem_5MiB.bin
mkfs.fat -F 12 -S 4096 -s 1 -n LVWW_UI build/production/lvww_filesystem_5MiB.bin
mcopy -s -i build/production/lvww_filesystem_5MiB.bin \
  applications/lv_wifi_weather/resources/qspi/ui ::/
fsck.fat -vn build/production/lvww_filesystem_5MiB.bin
mdir -s -i build/production/lvww_filesystem_5MiB.bin ::/ui
```

Verify all of the following:

- the image size is exactly `5242880` bytes;
- `mdir` shows `/ui/fonts/...fnt` and `/ui/data/cities_zh.tsv`;
- the image contains `/ui/...`, without an extra `qspi` or second `ui` directory;
- `fsck.fat` reports no filesystem errors.

Use PowerShell to check the size and hash:

```powershell
Get-Item .\build\production\lvww_filesystem_5MiB.bin |
  Select-Object Name, Length
Get-FileHash .\build\production\lvww_filesystem_5MiB.bin -Algorithm SHA256
```

### 3.2 Golden-board readback method (alternative)

If the host FAT tools cannot create an image with 4096-byte sectors, let this firmware format `filesystem` on a sample board. Transfer the complete `qspi/ui` contents into the corresponding `/ui` paths using YMODEM. After verifying the font and city search, use a validated board-specific QSPI external loader to read `0x00500000` bytes from `0x90300000` into a golden image.

A typical STM32CubeProgrammer CLI readback command is shown below. Confirm the exact arguments with the help for the installed version:

```powershell
& $Programmer -c "port=SWD sn=$Serial" -el $Loader `
  -u .\build\production\lvww_filesystem_5MiB.bin 0x90300000 0x00500000
```

Program the readback image into a second blank sample board and validate it again. File size alone does not prove that the image is usable.

## 4. Build the APP and create signed packages

### 4.1 Keep version fields consistent

Before a release, update these definitions in `applications/ota/ota_config.h`:

```c
#define OTA_CURRENT_FIRMWARE_VERSION_CODE  0x00010004UL
#define OTA_CURRENT_FIRMWARE_VERSION_TEXT  "V1.0.4"
```

`VERSION_CODE` must increase monotonically and exactly match both `mkimage.py --version` and the OTA responder's `--version-code`. The `V1.0.4` string is for display only; devices compare the numeric version.

### 4.2 Build the Release APP

A Release build in RT-Thread Studio normally produces:

```text
Release/rtthread.bin
```

Alternatively, use CMake:

```powershell
cmake --preset rt-studio-release
cmake --build --preset rt-studio-release
```

The CMake output is:

```text
build/cmake-release/rtthread.bin
```

Confirm that the build succeeds and that the BIN is no larger than `1044480` bytes: the 1 MiB firmware slot minus the 4 KiB header sector. `mkimage.py` also checks that the vector table is linked for `0x08100000`.

### 4.3 Prepare the signing key

Package signing requires `cryptography`:

```powershell
python -m pip install cryptography
```

For initial development, generate a P-256 key:

```powershell
python .\tools\ota_signing_key.py
```

The default private-key path is `tools/keys/ota_signing_private.pem`, which is ignored by Git. The script also generates the APP public-key header at `applications/ota/ota_trusted_key.h`. Copy that same public-key header to `bootloader/ota_trusted_key.h` in the Bootloader repository, then rebuild the Bootloader.

Production requirements:

- never commit the private key to Git or copy it to the file server or ordinary programming computers;
- manage the production key on an offline signing machine or in an HSM;
- do not routinely use `--force-new-key`, because deployed devices will not trust packages signed by an uncoordinated replacement key;
- ensure that the public-key fingerprints embedded in the APP and Bootloader match;
- use `--unsigned` only for a controlled, one-time migration of legacy devices, never for normal releases.

### 4.4 Create the OTA package and production slot image

The following example uses the CMake Release output and version `V1.0.4 / 0x00010004`:

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

When using RT-Thread Studio output, change the input path to `Release/rtthread.bin`.

The first package is compact and intended for HTTP downloads. The second is padded with `0xFF` to a complete 1 MiB slot for full-slot programmer erase/write operations. Both contain valid signatures over the same version and APP digest. Because ECDSA signing may use a random nonce, the signature bytes and file hashes of the two independently created packages are not required to match.

Record the release hashes:

```powershell
Get-FileHash .\build\production\h743_V1.0.4*.fwpkg -Algorithm SHA256
```

## 5. Deploy the signed APP package to the file server

See [SERVER_ENVIRONMENT_SETUP.md](SERVER_ENVIRONMENT_SETUP.md) for complete server setup instructions. This section covers the steps performed for each firmware release.

### 5.1 Windows Caddy

Copy to a temporary file, verify it, and then rename it atomically so devices cannot download a partially copied package:

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

The resulting download URL is:

```text
http://<server-IP>:8000/firmware/h743/V1.0.4.fwpkg
```

Replacing a static file does not require a Caddy restart. Prefer a new immutable filename for every release; do not overwrite a file while devices may be downloading it.

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

### 5.3 Validate the download and MQTT metadata

First test the URL from a network that can reach the device-facing server address:

```powershell
curl.exe -I http://<server-IP>:8000/firmware/h743/V1.0.4.fwpkg
```

The project's test responder downloads the server copy, compares it byte-for-byte with the local `--package`, and automatically calculates the package size, CRC32, and SHA-256:

```powershell
python .\tools\ota_mqtt_responder.py `
  --package .\build\production\h743_V1.0.4.fwpkg `
  --url http://<server-IP>:8000/firmware/h743/V1.0.4.fwpkg `
  --version V1.0.4 `
  --version-code 0x00010004
```

Start device upgrades only after `HTTP package check: OK` appears. Do not use `--skip-http-check` for a production release.

## 6. Batch programming with STM32CubeProgrammer

### 6.1 Prerequisites

Prepare the following tools and files:

- `STM32_Programmer_CLI.exe`;
- a tested `.stldr` external loader matching this board's QSPI pins, timing, and W25Q64 device;
- the Bootloader BIN for complete-device production programming;
- `h743_Vx.y.z_factory_1MiB.fwpkg`;
- `lvww_filesystem_5MiB.bin`;
- the ST-LINK serial number for each station.

This repository does not contain a board-specific `.stldr`. Do not select another development board's loader merely because it also uses a W25Q64. Identical Flash parts do not imply identical QSPI pins or initialization parameters.

Confirm that the CLI can see the probes:

```powershell
& $Programmer -l
```

Connect Under Reset with hardware reset is recommended. Adjust the connection settings only after validating the actual reset wiring on a sample board.

### 6.2 Recommended signed production flow

Program the complete signed package into both `upgrade` and `factory`:

1. Program the Bootloader into internal Flash.
2. Program the signed package into `upgrade`; on the first boot, the Bootloader verifies and installs it into the internal APP area.
3. Program the same signed package into `factory` for future recovery.
4. Program the resource FAT image into `filesystem`.
5. Reset and check the Bootloader log, APP version, font, and city search.

This makes both the initial installation and future recovery pass through Bootloader signature verification. Leave the `download` partition erased.

The following PowerShell example programs boards sequentially by ST-LINK serial number. Replace every path and serial number with the production values:

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

STM32CubeProgrammer versions may differ in whether `-el` accepts a full path or requires the name of an installed loader. Confirm the installed version with `STM32_Programmer_CLI.exe --help` and test one board before production. The script stops on the first failed step; never mix a failed board into the passed batch.

### 6.3 Directly program the internal APP (development/repair option)

The raw `rtthread.bin` can be written directly to `0x08100000`:

```powershell
$App = ".\build\cmake-release\rtthread.bin"

foreach ($Serial in $ProbeSerials) {
  $Connection = @("port=SWD", "sn=$Serial", "mode=UR", "reset=HWrst")
  & $Programmer -c $Connection -d $App 0x08100000 -v -rst
  if ($LASTEXITCODE -ne 0) { throw "APP failed: $Serial" }
}
```

`rtthread.bin` is a raw APP image and does not contain the `.fwpkg` header or ECDSA signature. Direct programming bypasses Bootloader package verification. If production requires a verified signing chain, use the preceding flow in which the Bootloader installs the signed `upgrade` package. Even when directly programming the APP, program a valid signed package into `factory` for recovery.

### 6.4 Post-programming sampling checks

For every batch, sample and verify at least the following:

- the UART1 Bootloader log contains no header, CRC, SHA-256, or signature errors;
- the APP version matches the production order;
- `ls -l /ui/fonts` shows the font size recorded in `manifest.json`;
- `ls -l /ui/data` shows `cities_zh.tsv`;
- `uires_reload` succeeds and Chinese text renders correctly;
- searching in Chinese, English, or Pinyin finds at least two non-default cities;
- a sampled factory recovery or OTA upgrade succeeds, proving that the public key and signature chain match;
- the batch record contains the SHA-256 values of the Bootloader, APP, signed package, and resource image, together with the programmer logs.

## 7. Troubleshooting

| Symptom | Likely cause | Corrective action |
| --- | --- | --- |
| The APP formats QSPI again after boot | FAT sector size is not 4096, the image is corrupt, or it was programmed at the wrong address | Rebuild the 5 MiB image and check the external loader base address |
| The font or city list is missing | The image contains one directory level too many or too few | Inspect the internal paths with `mdir -s` and ensure they start at `/ui/...` |
| `mkimage.py` reports an invalid vector table | The wrong BIN was selected or the APP was not linked for `0x08100000` | Check the linker script and build output |
| The Bootloader reports signature failure | The packaging private key does not match the APP/Bootloader public key | Compare both public-key header fingerprints, rebuild, and sign again |
| The device reports no update | `version_code` did not increase or the MQTT version differs from the package header | Use the same numeric version in configuration, packaging, and the responder |
| HTTP validation fails | The URL points to an old file, copying is incomplete, or the Caddy root differs | Compare local/server SHA-256 values and deploy through an atomic temporary-file rename |
| External Flash programming fails | The `.stldr` does not match the board's QSPI pins or timing | Stop the batch and validate or build a board-specific external loader |
