#!/usr/bin/env python3
"""Regenerate every firmware and QSPI resource used by LV Wi-Fi Weather."""

from __future__ import annotations

import argparse
from pathlib import Path

from generate_font import generate as generate_builtin_font
from generate_qspi_assets import generate as generate_qspi_assets
from source_han_sans import acquire_source_han_sans


def main() -> None:
    script_dir = Path(__file__).resolve().parent
    module_root = script_dir.parent
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--font-cache",
        type=Path,
        default=script_dir / ".cache" / "source-han-sans",
    )
    parser.add_argument(
        "--city-source",
        type=Path,
        default=script_dir / "China-City-List-latest.csv",
    )
    parser.add_argument(
        "--output-root",
        type=Path,
        default=module_root / "resources" / "qspi" / "ui",
    )
    parser.add_argument(
        "--builtin-font-output",
        type=Path,
        default=module_root / "src" / "lvww_font_cjk_16.c",
    )
    args = parser.parse_args()

    font_path = acquire_source_han_sans(args.font_cache)
    generate_builtin_font(module_root, font_path, args.builtin_font_output)
    generate_qspi_assets(
        module_root,
        font_path,
        args.output_root,
        args.city_source,
    )
    print("all LV Wi-Fi Weather resources generated successfully")


if __name__ == "__main__":
    main()
