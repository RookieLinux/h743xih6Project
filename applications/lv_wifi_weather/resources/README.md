# QSPI UI resources

Run the one-command generator, then copy the generated `qspi/ui` directory to
`/ui` on the QSPI FAT filesystem.

```text
python tools/generate_all_resources.py
```

Runtime layout:

```text
/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt
/ui/fonts/LICENSE-SourceHanSans.txt
/ui/data/cities_zh.tsv
/ui/images/*.bin
/ui/text/common_chars_utf8.txt
/ui/manifest.json
```

All source and metadata text files use UTF-8 without BOM. The `.fnt` file is a
little-endian binary font indexed by Unicode code point. Glyph alpha depth is
4 bpp. Images should use the LVGL 8 binary image format and can be referenced as
`Q:/ui/images/<name>.bin`.

The font generator downloads Adobe Source Han Sans SC Regular from the official
`adobe-fonts/source-han-sans` GitHub release. The pinned release, download URL,
archive SHA-256, extracted font SHA-256, and generated resource SHA-256 are
recorded in `manifest.json`. The downloaded OTF is cached under
`tools/.cache/source-han-sans` and is not committed to the project.
Source Han Sans is distributed under the SIL Open Font License 1.1; see the
official `LICENSE.txt` in the upstream repository.

`cities_zh.tsv` is the local Chinese city search database. It is generated
from QWeather's public `China-City-List-latest.csv`; the checked-in source
version and hashes are recorded in `manifest.json`. Chinese names are always
used for display, while English names are retained only as search keys.

To update the database, replace `tools/China-City-List-latest.csv` with the
latest file from <https://github.com/qwd/LocationList> and run:

```text
python tools/generate_all_resources.py
```

The first run downloads the official Simplified Chinese OTF release archive.
To deliberately use a local font instead, pass `--font path/to/font.otf`.

The same command regenerates the small font compiled into the firmware, the
QSPI `.fnt`, `cities_zh.tsv`, `manifest.json`, character list, and font license.

After replacing resources while the device is running, execute
`uires_reload` in the RT-Thread shell.

The firmware creates `/ui/fonts`, `/ui/images`, `/ui/text`, and `/ui/data`
automatically.
Transfer the files through any DFS-capable channel available on the board. For
example, when RT-Thread YMODEM file transfer is enabled, receive the files with
`ry`, move them into the directories above, and then run `uires_reload`.

Do not program the `.fnt` file directly to a raw flash offset. It must be stored
as a normal file inside the mounted FAT filesystem. The current FAL partition is
5 MiB and starts at QSPI offset `0x00300000`.
