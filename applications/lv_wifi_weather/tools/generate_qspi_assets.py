#!/usr/bin/env python3
"""Build QSPI filesystem resources for the LVGL Wi-Fi weather UI."""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import struct
import unicodedata
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


MAGIC = b"LVWWFNT1"
VERSION = 1
PIXEL_SIZE = 16
LINE_HEIGHT = 19
BASE_LINE = 3
GLYPH_OFS_Y = -2
BPP = 4
GLYPH_BYTES = PIXEL_SIZE * PIXEL_SIZE * BPP // 8
HEADER_FORMAT = "<8sHHHHhhBBIIIHH"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)


def collect_gb2312_characters() -> set[str]:
    characters: set[str] = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                text = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            for char in text:
                if char.isprintable() and not unicodedata.category(char).startswith("C"):
                    characters.add(char)
    return characters


def collect_project_characters(module_root: Path) -> set[str]:
    characters: set[str] = set()
    for pattern in ("*.c", "*.h"):
        for path in module_root.rglob(pattern):
            if path.name == "lvww_font_cjk_16.c":
                continue
            text = path.read_text(encoding="utf-8")
            characters.update(re.findall(r"[\u00a0-\u9fff]", text))
    return characters


def render_glyph(font: ImageFont.FreeTypeFont, char: str) -> bytes:
    image = Image.new("L", (PIXEL_SIZE, PIXEL_SIZE), 0)
    draw = ImageDraw.Draw(image)
    # Preserve the common font baseline.  Aligning each glyph by its own
    # bounding box incorrectly moves characters such as "一" to the top.
    draw.text((0, 0), char, font=font, fill=255)

    packed = bytearray()
    for y in range(PIXEL_SIZE):
        for x in range(0, PIXEL_SIZE, 2):
            high = min(15, (image.getpixel((x, y)) + 8) // 17)
            low = min(15, (image.getpixel((x + 1, y)) + 8) // 17)
            packed.append((high << 4) | low)
    if len(packed) != GLYPH_BYTES:
        raise RuntimeError("unexpected packed glyph size")
    return bytes(packed)


def build_font(font_path: Path, output: Path, codepoints: list[int]) -> None:
    font = ImageFont.truetype(str(font_path), PIXEL_SIZE)
    codepoint_offset = HEADER_SIZE
    bitmap_offset = codepoint_offset + len(codepoints) * 4
    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        HEADER_SIZE,
        PIXEL_SIZE,
        LINE_HEIGHT,
        BASE_LINE,
        GLYPH_OFS_Y,
        BPP,
        0,
        len(codepoints),
        codepoint_offset,
        bitmap_offset,
        GLYPH_BYTES,
        0,
    )

    output.parent.mkdir(parents=True, exist_ok=True)
    with output.open("wb") as stream:
        stream.write(header)
        for codepoint in codepoints:
            stream.write(struct.pack("<I", codepoint))
        for codepoint in codepoints:
            stream.write(render_glyph(font, chr(codepoint)))


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(64 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_utf8(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def generate(module_root: Path, font_path: Path, output_root: Path) -> None:
    characters = collect_gb2312_characters()
    # Online geocoding results are not limited to GB2312.  Cover the complete
    # basic CJK block and Latin accents commonly found in international city
    # names; the resulting ~2.8 MiB font still fits the 5 MiB filesystem.
    characters.update(
        char
        for codepoint in range(0x4E00, 0xA000)
        if (char := chr(codepoint)).isprintable()
        and not unicodedata.category(char).startswith("C")
    )
    characters.update(
        char
        for codepoint in range(0x00A0, 0x0250)
        if (char := chr(codepoint)).isprintable()
        and not unicodedata.category(char).startswith("C")
    )
    characters.update(collect_project_characters(module_root))
    characters.update("℃°，。！？：；（）【】《》“”‘’、…—·￥")
    codepoints = sorted({ord(char) for char in characters})

    font_output = output_root / "fonts" / "lvww_simsun_16_4bpp.fnt"
    chars_output = output_root / "text" / "common_chars_utf8.txt"
    images_readme = output_root / "images" / "README.txt"
    manifest_output = output_root / "manifest.json"

    build_font(font_path, font_output, codepoints)
    write_utf8(chars_output, "".join(chr(codepoint) for codepoint in codepoints) + "\n")
    write_utf8(
        images_readme,
        "此目录用于保存后续 LVGL 图形资源。\n"
        "文件名统一使用 ASCII，文本元数据统一使用无 BOM UTF-8。\n"
        "推荐将图片转换为 LVGL 8 原生二进制 .bin 格式，运行时使用：\n"
        "lv_img_set_src(image, \"Q:/ui/images/example.bin\");\n",
    )

    manifest = {
        "schema": 1,
        "encoding": "UTF-8 without BOM",
        "lvgl_drive": "Q:",
        "filesystem_root": "/ui",
        "font": {
            "path": "/ui/fonts/lvww_simsun_16_4bpp.fnt",
            "format": "lvww-font-v1 little-endian",
            "source": font_path.name,
            "pixel_size": PIXEL_SIZE,
            "bits_per_pixel": BPP,
            "glyph_count": len(codepoints),
            "glyph_cache_count": 8,
            "sha256": sha256(font_output),
            "size_bytes": font_output.stat().st_size,
        },
        "text": {
            "path": "/ui/text/common_chars_utf8.txt",
            "encoding": "UTF-8 without BOM",
            "sha256": sha256(chars_output),
        },
        "images": {
            "path": "/ui/images",
            "preferred_format": "LVGL 8 binary image (.bin)",
            "lvgl_path_example": "Q:/ui/images/example.bin",
        },
    }
    write_utf8(manifest_output, json.dumps(manifest, ensure_ascii=False, indent=2) + "\n")
    print(
        f"generated {font_output} with {len(codepoints)} glyphs, "
        f"{font_output.stat().st_size} bytes"
    )


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    module_root = script_dir.parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--font", type=Path, default=Path(r"C:\Windows\Fonts\simsun.ttc"))
    parser.add_argument(
        "--output-root",
        type=Path,
        default=module_root / "resources" / "qspi" / "ui",
    )
    args = parser.parse_args()
    generate(module_root, args.font, args.output_root)


if __name__ == "__main__":
    main()
