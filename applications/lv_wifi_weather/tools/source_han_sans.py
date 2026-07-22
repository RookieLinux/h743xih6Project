"""Download and cache the official Source Han Sans Simplified Chinese font."""

from __future__ import annotations

import hashlib
import shutil
import tempfile
import urllib.request
import zipfile
from pathlib import Path


SOURCE_HAN_SANS_RELEASE = "2.005R"
SOURCE_HAN_SANS_ARCHIVE_URL = (
    "https://github.com/adobe-fonts/source-han-sans/releases/download/"
    f"{SOURCE_HAN_SANS_RELEASE}/09_SourceHanSansSC.zip"
)
SOURCE_HAN_SANS_ARCHIVE_SHA256 = (
    "ef7364f7ac2564be1ae9c1d74276de2653fe38b73449070398c4fc0b7e032ff1"
)
SOURCE_HAN_SANS_FONT_NAME = "SourceHanSansSC-Regular.otf"
SOURCE_HAN_SANS_FONT_SHA256 = (
    "f1d8611151880c6c336aabeac4640ef434fa13cbfbf1ffe82d0a71b2a5637256"
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _download(url: str, destination: Path) -> None:
    request = urllib.request.Request(
        url,
        headers={"User-Agent": "lvww-source-han-sans-font-generator"},
    )
    with urllib.request.urlopen(request, timeout=60) as response:
        with destination.open("wb") as stream:
            shutil.copyfileobj(response, stream, length=1024 * 1024)


def acquire_source_han_sans(cache_root: Path) -> Path:
    """Return the cached Regular SC OTF, downloading the official release once."""
    release_dir = cache_root / SOURCE_HAN_SANS_RELEASE
    font_path = release_dir / SOURCE_HAN_SANS_FONT_NAME
    if font_path.is_file() and sha256(font_path) == SOURCE_HAN_SANS_FONT_SHA256:
        return font_path
    if font_path.exists():
        font_path.unlink()

    release_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="lvww-source-han-sans-") as temp_dir:
        archive_path = Path(temp_dir) / "SourceHanSansSC.zip"
        print(f"downloading {SOURCE_HAN_SANS_ARCHIVE_URL}")
        _download(SOURCE_HAN_SANS_ARCHIVE_URL, archive_path)
        actual_hash = sha256(archive_path)
        if actual_hash != SOURCE_HAN_SANS_ARCHIVE_SHA256:
            raise RuntimeError(
                "Source Han Sans archive SHA-256 mismatch: "
                f"expected {SOURCE_HAN_SANS_ARCHIVE_SHA256}, got {actual_hash}"
            )

        with zipfile.ZipFile(archive_path) as archive:
            members = [
                name for name in archive.namelist()
                if Path(name).name == SOURCE_HAN_SANS_FONT_NAME
            ]
            if len(members) != 1:
                raise RuntimeError(
                    f"cannot uniquely locate {SOURCE_HAN_SANS_FONT_NAME} in archive"
                )
            with archive.open(members[0]) as source, font_path.open("wb") as target:
                shutil.copyfileobj(source, target, length=1024 * 1024)

        actual_font_hash = sha256(font_path)
        if actual_font_hash != SOURCE_HAN_SANS_FONT_SHA256:
            font_path.unlink(missing_ok=True)
            raise RuntimeError(
                "Source Han Sans font SHA-256 mismatch: "
                f"expected {SOURCE_HAN_SANS_FONT_SHA256}, got {actual_font_hash}"
            )

    print(f"cached {font_path}")
    return font_path
