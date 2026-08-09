#!/usr/bin/env python3
"""Build a commit-last QSPI firmware package for the H743 bootloader."""

from __future__ import annotations

import argparse
import hashlib
import os
import pathlib
import struct
import zlib


MAGIC = 0x544F4F42
FORMAT_VERSION = 1
HEADER_SIZE = 256
HEADER_FLAGS_VALID = 1
HARDWARE_ID = 0x48373433
APP_BASE = 0x08100000
PAYLOAD_OFFSET = 4096
SLOT_SIZE = 1024 * 1024
MAX_IMAGE_SIZE = SLOT_SIZE - PAYLOAD_OFFSET
SIGNATURE_NONE = 0
SIGNATURE_ECDSA_P256_RAW = 1
SIGNATURE_SIZE = 64
SIGNATURE_DOMAIN = b"H743OTA1"
P256_ORDER = int(
    "FFFFFFFF00000000FFFFFFFFFFFFFFFFBCE6FAADA7179E84F3B9CAC2FC632551",
    16,
)

# Fields through signature, followed by reserved bytes and header CRC32.
HEADER_PREFIX = struct.Struct("<IHHIIIIIII32sHH64s")
SIGNED_DATA = struct.Struct("<8sIHHIIIIIII32sHH")
RESERVED_SIZE = HEADER_SIZE - HEADER_PREFIX.size - 4


def app_vector_is_valid(image: bytes) -> bool:
    if len(image) < 8:
        return False
    stack_pointer, reset_handler = struct.unpack_from("<II", image, 0)
    stack_in_ram = (
        0x20000000 <= stack_pointer <= 0x20020000
        or 0x24000000 <= stack_pointer <= 0x24080000
        or 0x30000000 <= stack_pointer <= 0x30048000
        or 0x38000000 <= stack_pointer <= 0x38010000
    )
    return (
        stack_pointer % 8 == 0
        and stack_in_ram
        and reset_handler & 1 == 1
        and APP_BASE <= (reset_handler & ~1) < APP_BASE + SLOT_SIZE
    )


def load_signing_key(path: pathlib.Path, password: bytes | None):
    try:
        from cryptography.hazmat.primitives import serialization
        from cryptography.hazmat.primitives.asymmetric import ec
    except ImportError as error:
        raise ValueError(
            "the 'cryptography' package is required for signed packages"
        ) from error

    key = serialization.load_pem_private_key(path.read_bytes(), password=password)
    if not isinstance(key, ec.EllipticCurvePrivateKey) or not isinstance(
        key.curve, ec.SECP256R1
    ):
        raise ValueError("the OTA signing key must use ECDSA P-256 (secp256r1)")
    return key


def build_signed_data(
    firmware_version: int,
    image_size: int,
    image_crc32: int,
    image_sha256: bytes,
    signature_algorithm: int,
    signature_size: int,
) -> bytes:
    return SIGNED_DATA.pack(
        SIGNATURE_DOMAIN,
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        HEADER_FLAGS_VALID,
        HARDWARE_ID,
        firmware_version,
        PAYLOAD_OFFSET,
        image_size,
        APP_BASE,
        image_crc32,
        image_sha256,
        signature_algorithm,
        signature_size,
    )


def sign_metadata(signing_key, signed_data: bytes) -> bytes:
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.primitives.asymmetric import ec
    from cryptography.hazmat.primitives.asymmetric.utils import (
        decode_dss_signature,
        encode_dss_signature,
    )

    der_signature = signing_key.sign(signed_data, ec.ECDSA(hashes.SHA256()))
    r_value, s_value = decode_dss_signature(der_signature)
    if s_value > P256_ORDER // 2:
        s_value = P256_ORDER - s_value
    canonical_der = encode_dss_signature(r_value, s_value)
    signing_key.public_key().verify(
        canonical_der, signed_data, ec.ECDSA(hashes.SHA256())
    )
    return r_value.to_bytes(32, "big") + s_value.to_bytes(32, "big")


def build_header(
    image: bytes,
    firmware_version: int,
    signing_key=None,
) -> bytes:
    if not 8 <= len(image) <= MAX_IMAGE_SIZE:
        raise ValueError(
            f"image size {len(image)} is outside 8..{MAX_IMAGE_SIZE} bytes"
        )
    if not app_vector_is_valid(image):
        raise ValueError(
            "input vector table is not linked for APP_BASE 0x08100000 "
            "or has an invalid initial stack pointer"
        )

    image_crc32 = zlib.crc32(image) & 0xFFFFFFFF
    image_sha256 = hashlib.sha256(image).digest()
    signature_algorithm = (
        SIGNATURE_ECDSA_P256_RAW if signing_key is not None else SIGNATURE_NONE
    )
    signature_size = SIGNATURE_SIZE if signing_key is not None else 0
    signature = bytes(SIGNATURE_SIZE)
    if signing_key is not None:
        signed_data = build_signed_data(
            firmware_version,
            len(image),
            image_crc32,
            image_sha256,
            signature_algorithm,
            signature_size,
        )
        signature = sign_metadata(signing_key, signed_data)
    prefix = HEADER_PREFIX.pack(
        MAGIC,
        FORMAT_VERSION,
        HEADER_SIZE,
        HEADER_FLAGS_VALID,
        HARDWARE_ID,
        firmware_version,
        PAYLOAD_OFFSET,
        len(image),
        APP_BASE,
        image_crc32,
        image_sha256,
        signature_algorithm,
        signature_size,
        signature,
    )
    header_without_crc = prefix + bytes(RESERVED_SIZE) + bytes(4)
    header_crc32 = zlib.crc32(header_without_crc) & 0xFFFFFFFF
    return header_without_crc[:-4] + struct.pack("<I", header_crc32)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Wrap an APP binary in the 256-byte boot header and place the "
            "payload at offset 0x1000. Program the result at QSPI offset "
            "0x000000 (upgrade) or 0x100000 (factory). Offset 0x200000 is "
            "reserved for application-side download staging."
        )
    )
    parser.add_argument("input", type=pathlib.Path, help="APP .bin file")
    parser.add_argument("output", type=pathlib.Path, help="output .fwpkg file")
    parser.add_argument(
        "--version",
        required=True,
        type=lambda value: int(value, 0),
        help="monotonic firmware version, decimal or 0x-prefixed",
    )
    signing = parser.add_mutually_exclusive_group(required=True)
    signing.add_argument(
        "--signing-key",
        type=pathlib.Path,
        help="ECDSA P-256 private key in PEM format",
    )
    signing.add_argument(
        "--unsigned",
        action="store_true",
        help="create a legacy unsigned package for controlled migration only",
    )
    parser.add_argument(
        "--key-password-env",
        help="environment variable containing the private-key password",
    )
    parser.add_argument(
        "--pad-slot",
        action="store_true",
        help="pad the package to the complete 1 MiB QSPI slot",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    image = args.input.read_bytes()
    try:
        password = None
        if args.key_password_env:
            password_text = os.environ.get(args.key_password_env)
            if password_text is None:
                raise ValueError(
                    f"password environment variable is not set: {args.key_password_env}"
                )
            password = password_text.encode("utf-8")
        signing_key = (
            load_signing_key(args.signing_key, password)
            if args.signing_key is not None
            else None
        )
        header = build_header(image, args.version, signing_key)
    except (OSError, ValueError) as error:
        raise SystemExit(f"error: {error}") from error
    package = header + bytes([0xFF]) * (PAYLOAD_OFFSET - len(header)) + image
    if args.pad_slot:
        package += bytes([0xFF]) * (SLOT_SIZE - len(package))

    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(package)

    print(f"input:       {args.input}")
    print(f"output:      {args.output}")
    print(f"version:     {args.version}")
    print(
        "signature:   ECDSA P-256 (raw r||s)"
        if signing_key is not None
        else "signature:   NONE (migration only)"
    )
    print(f"image size:  {len(image)} bytes")
    print(f"package:     {len(package)} bytes")
    print(f"CRC32:       {zlib.crc32(image) & 0xFFFFFFFF:08X}")
    print(f"SHA-256:     {hashlib.sha256(image).hexdigest()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
