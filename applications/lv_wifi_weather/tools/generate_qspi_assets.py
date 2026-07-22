#!/usr/bin/env python3
"""Build QSPI filesystem resources for the LVGL Wi-Fi weather UI."""

from __future__ import annotations

import argparse
import csv
import json
import re
import shutil
import struct
import unicodedata
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

from source_han_sans import (
    SOURCE_HAN_SANS_ARCHIVE_SHA256,
    SOURCE_HAN_SANS_ARCHIVE_URL,
    SOURCE_HAN_SANS_RELEASE,
    acquire_source_han_sans,
    sha256,
)


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
CITY_DB_MAGIC = "#LVWW_CITY_DB_V1"
FONT_LICENSE_SOURCE_URL = (
    "https://github.com/adobe-fonts/source-han-sans/blob/2.005R/LICENSE.txt"
)
CITY_DB_SOURCE = (
    "https://github.com/qwd/LocationList/blob/master/"
    "China-City-List-latest.csv"
)
CITY_DB_COLUMNS = (
    "id",
    "name_en",
    "name_zh",
    "adm1_zh",
    "adm2_zh",
    "country_zh",
    "timezone",
    "latitude",
    "longitude",
    "adcode",
)


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
    # Source Han Sans has a taller global ascender than the 16 px bitmap box.
    # Place every glyph on one explicit baseline so the bottom is not clipped
    # and low horizontal strokes such as "一" stay vertically centered.
    draw.text((0, PIXEL_SIZE - 3), char, font=font, fill=255, anchor="ls")

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


def write_utf8(path: Path, text: str) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text, encoding="utf-8", newline="\n")


def clean_tsv(value: str) -> str:
    return value.replace("\t", " ").replace("\r", " ").replace("\n", " ").strip()


def build_city_database(source: Path, output: Path) -> tuple[int, str]:
    records: list[str] = []
    with source.open("r", encoding="utf-8-sig", newline="") as stream:
        reader = csv.reader(stream)
        try:
            version_row = next(reader)
            source_columns = next(reader)
        except StopIteration as exc:
            raise RuntimeError("city source is missing its header") from exc
        source_version = clean_tsv(version_row[0]) if version_row else "unknown"
        column_index = {name: index for index, name in enumerate(source_columns)}
        required = (
            "Location_ID",
            "Location_Name_EN",
            "Location_Name_ZH",
            "Country_Region_ZH",
            "Adm1_Name_ZH",
            "Adm2_Name_ZH",
            "Timezone",
            "Latitude",
            "Longitude",
            "AD_code",
        )
        missing = [name for name in required if name not in column_index]
        if missing:
            raise RuntimeError(f"city source missing columns: {', '.join(missing)}")

        for row in reader:
            if len(row) < len(source_columns):
                continue
            values = (
                row[column_index["Location_ID"]],
                row[column_index["Location_Name_EN"]],
                row[column_index["Location_Name_ZH"]],
                row[column_index["Adm1_Name_ZH"]],
                row[column_index["Adm2_Name_ZH"]],
                row[column_index["Country_Region_ZH"]],
                row[column_index["Timezone"]],
                row[column_index["Latitude"]],
                row[column_index["Longitude"]],
                row[column_index["AD_code"]],
            )
            values = tuple(clean_tsv(value) for value in values)
            if not values[0] or not values[2] or not values[7] or not values[8]:
                continue
            records.append("\t".join(values))

    write_utf8(
        output,
        CITY_DB_MAGIC + "\n" + "\t".join(CITY_DB_COLUMNS) + "\n" +
        "\n".join(records) + "\n",
    )
    return len(records), source_version


def generate(module_root: Path, font_path: Path, output_root: Path,
             city_source: Path) -> None:
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

    font_output = output_root / "fonts" / "lvww_source_han_sans_sc_16_4bpp.fnt"
    license_source = module_root / "tools" / "licenses" / "LICENSE-SourceHanSans.txt"
    license_output = output_root / "fonts" / "LICENSE-SourceHanSans.txt"
    chars_output = output_root / "text" / "common_chars_utf8.txt"
    images_readme = output_root / "images" / "README.txt"
    city_output = output_root / "data" / "cities_zh.tsv"
    manifest_output = output_root / "manifest.json"

    build_font(font_path, font_output, codepoints)
    license_output.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(license_source, license_output)
    write_utf8(chars_output, "".join(chr(codepoint) for codepoint in codepoints) + "\n")
    write_utf8(
        images_readme,
        "此目录用于保存后续 LVGL 图形资源。\n"
        "文件名统一使用 ASCII，文本元数据统一使用无 BOM UTF-8。\n"
        "推荐将图片转换为 LVGL 8 原生二进制 .bin 格式，运行时使用：\n"
        "lv_img_set_src(image, \"Q:/ui/images/example.bin\");\n",
    )
    city_count, city_version = build_city_database(city_source, city_output)
    print(f"generated {city_output} with {city_count} Chinese locations")

    manifest = {
        "schema": 1,
        "encoding": "UTF-8 without BOM",
        "lvgl_drive": "Q:",
        "filesystem_root": "/ui",
        "font": {
            "path": "/ui/fonts/lvww_source_han_sans_sc_16_4bpp.fnt",
            "format": "lvww-font-v1 little-endian",
            "source": font_path.name,
            "source_project": "adobe-fonts/source-han-sans",
            "source_release": SOURCE_HAN_SANS_RELEASE,
            "source_url": SOURCE_HAN_SANS_ARCHIVE_URL,
            "source_archive_sha256": SOURCE_HAN_SANS_ARCHIVE_SHA256,
            "source_font_sha256": sha256(font_path),
            "pixel_size": PIXEL_SIZE,
            "bits_per_pixel": BPP,
            "glyph_count": len(codepoints),
            "glyph_cache_count": 8,
            "sha256": sha256(font_output),
            "size_bytes": font_output.stat().st_size,
            "license": {
                "path": "/ui/fonts/LICENSE-SourceHanSans.txt",
                "name": "SIL Open Font License 1.1",
                "source": FONT_LICENSE_SOURCE_URL,
                "sha256": sha256(license_output),
            },
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
        "city_database": {
            "path": "/ui/data/cities_zh.tsv",
            "format": "lvww-city-db-v1 tab-separated",
            "encoding": "UTF-8 without BOM",
            "source": CITY_DB_SOURCE,
            "source_version": city_version,
            "source_sha256": sha256(city_source),
            "record_count": city_count,
            "sha256": sha256(city_output),
            "size_bytes": city_output.stat().st_size,
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
    parser.add_argument(
        "--font",
        type=Path,
        help="use a local font instead of downloading Source Han Sans SC",
    )
    parser.add_argument(
        "--font-cache",
        type=Path,
        default=module_root / "tools" / ".cache" / "source-han-sans",
        help="cache directory for the downloaded Adobe font",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=module_root / "resources" / "qspi" / "ui",
    )
    parser.add_argument(
        "--city-source",
        type=Path,
        default=module_root / "tools" / "China-City-List-latest.csv",
        help="QWeather China-City-List CSV used to build cities_zh.tsv",
    )
    args = parser.parse_args()
    font_path = args.font or acquire_source_han_sans(args.font_cache)
    generate(module_root, font_path, args.output_root, args.city_source)


if __name__ == "__main__":
    main()
