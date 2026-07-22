# QSPI UI resources

Run `tools/generate_qspi_assets.py`, then copy the generated `qspi/ui` directory
to `/ui` on the QSPI FAT filesystem.

Runtime layout:

```text
/ui/fonts/lvww_simsun_16_4bpp.fnt
/ui/images/*.bin
/ui/text/common_chars_utf8.txt
/ui/manifest.json
```

All source and metadata text files use UTF-8 without BOM. The `.fnt` file is a
little-endian binary font indexed by Unicode code point. Glyph alpha depth is
4 bpp. Images should use the LVGL 8 binary image format and can be referenced as
`Q:/ui/images/<name>.bin`.

After replacing resources while the device is running, execute
`uires_reload` in the RT-Thread shell.

The firmware creates `/ui/fonts`, `/ui/images`, and `/ui/text` automatically.
Transfer the files through any DFS-capable channel available on the board. For
example, when RT-Thread YMODEM file transfer is enabled, receive the files with
`ry`, move them into the directories above, and then run `uires_reload`.

Do not program the `.fnt` file directly to a raw flash offset. It must be stored
as a normal file inside the mounted FAT filesystem. The current FAL partition is
5 MiB and starts at QSPI offset `0x00300000`.
