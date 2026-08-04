#!/usr/bin/env python3
"""Reference client for the APP OTA Wi-Fi protocol v1."""

from __future__ import annotations

import argparse
import hashlib
import socket
import struct
import sys
import zlib
from pathlib import Path


MAGIC = 0x3141544F
VERSION = 1
HEADER = struct.Struct("<IHHIIII")
IMAGE_HEADER = struct.Struct("<IHHIIIIIII32sHH64s116sI")

MSG_QUERY = 1
MSG_BEGIN = 2
MSG_DATA = 3
MSG_END = 4
MSG_ABORT = 5
MSG_REBOOT = 6
RESPONSE_BIT = 0x8000

STATE_NAMES = {
    0: "uninitialized",
    1: "idle",
    2: "receiving",
    3: "validating",
    4: "committing",
    5: "ready",
    6: "error",
}


def inspect_package(package: bytes) -> tuple[int, int]:
    if len(package) < IMAGE_HEADER.size:
        raise ValueError("file is shorter than the 256-byte image header")
    values = IMAGE_HEADER.unpack_from(package)
    (
        magic,
        format_version,
        header_size,
        flags,
        hardware_id,
        firmware_version,
        payload_offset,
        image_size,
        load_address,
        image_crc,
        image_sha256,
        signature_algorithm,
        signature_size,
        _signature,
        _reserved,
        header_crc,
    ) = values
    if magic != 0x544F4F42 or format_version != 1 or header_size != 256:
        raise ValueError("not a supported boot_image.h package")
    if not flags & 1:
        raise ValueError("package valid flag is not set")
    if hardware_id != 0x48373433 or load_address != 0x08100000:
        raise ValueError("package targets a different hardware/load address")
    if payload_offset + image_size > len(package):
        raise ValueError("payload extends beyond the package")
    header_copy = bytearray(package[:256])
    header_copy[-4:] = b"\0\0\0\0"
    if zlib.crc32(header_copy) & 0xFFFFFFFF != header_crc:
        raise ValueError("image header CRC mismatch")
    payload = package[payload_offset : payload_offset + image_size]
    if zlib.crc32(payload) & 0xFFFFFFFF != image_crc:
        raise ValueError("image payload CRC mismatch")
    if hashlib.sha256(payload).digest() != image_sha256:
        raise ValueError("image payload SHA-256 mismatch")
    if signature_size > 64:
        raise ValueError("signature is too large")
    if signature_algorithm != 0:
        print(
            "warning: signed package requires matching signature verifier "
            "in APP and Bootloader",
            file=sys.stderr,
        )
    return firmware_version, image_size


def recv_exact(sock: socket.socket, size: int) -> bytes:
    result = bytearray()
    while len(result) < size:
        block = sock.recv(size - len(result))
        if not block:
            raise ConnectionError("device closed the connection")
        result.extend(block)
    return bytes(result)


class OtaClient:
    def __init__(self, host: str, port: int, timeout: float) -> None:
        self.sock = socket.create_connection((host, port), timeout=timeout)
        self.sock.settimeout(timeout)
        self.sequence = 0

    def close(self) -> None:
        self.sock.close()

    def request(
        self, message_type: int, payload: bytes = b"", offset: int = 0
    ) -> bytes:
        self.sequence += 1
        payload_crc = zlib.crc32(payload) & 0xFFFFFFFF if payload else 0
        frame = HEADER.pack(
            MAGIC,
            VERSION,
            message_type,
            self.sequence,
            offset,
            len(payload),
            payload_crc,
        )
        self.sock.sendall(frame + payload)

        response = recv_exact(self.sock, HEADER.size)
        magic, version, response_type, sequence, raw_status, length, crc = (
            HEADER.unpack(response)
        )
        if (
            magic != MAGIC
            or version != VERSION
            or response_type != (message_type | RESPONSE_BIT)
            or sequence != self.sequence
        ):
            raise RuntimeError("invalid response header")
        body = recv_exact(self.sock, length) if length else b""
        if (zlib.crc32(body) & 0xFFFFFFFF if body else 0) != crc:
            raise RuntimeError("response payload CRC mismatch")
        status = struct.unpack("<i", struct.pack("<I", raw_status))[0]
        if status != 0:
            raise RuntimeError(f"device rejected request: OTA error {status}")
        return body

    def query(self) -> tuple[int, int, int, int, int]:
        body = self.request(MSG_QUERY)
        if len(body) != 20:
            raise RuntimeError("invalid QUERY response size")
        state, raw_error, total, received, firmware = struct.unpack(
            "<IIIII", body
        )
        error = struct.unpack("<i", struct.pack("<I", raw_error))[0]
        return state, error, total, received, firmware

    def upload(self, package: bytes, reboot: bool, chunk_size: int) -> None:
        package_crc = zlib.crc32(package) & 0xFFFFFFFF
        flags = 1 if reboot else 0
        self.request(
            MSG_BEGIN,
            struct.pack("<III", len(package), package_crc, flags),
        )
        total = len(package)
        for offset in range(0, total, chunk_size):
            chunk = package[offset : offset + chunk_size]
            self.request(MSG_DATA, chunk, offset)
            sent = offset + len(chunk)
            print(
                f"\rtransferring {sent}/{total} bytes "
                f"({sent * 100 // total:3d}%)",
                end="",
                flush=True,
            )
        print("\ndevice is validating and committing the package...")
        self.request(MSG_END)
        print("upgrade committed successfully")

    def reboot(self) -> None:
        self.request(MSG_REBOOT)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("host", help="device IPv4 address")
    parser.add_argument("package", nargs="?", type=Path)
    parser.add_argument("--port", type=int, default=5000)
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--chunk-size", type=int, default=4096)
    parser.add_argument("--reboot", action="store_true")
    parser.add_argument("--query", action="store_true")
    args = parser.parse_args()

    if not 1 <= args.chunk_size <= 4096:
        parser.error("--chunk-size must be in 1..4096")
    if not args.query and args.package is None:
        parser.error("PACKAGE is required unless --query is used")

    client = OtaClient(args.host, args.port, args.timeout)
    try:
        if args.query:
            state, error, total, received, firmware = client.query()
            print(
                f"state={STATE_NAMES.get(state, state)} error={error} "
                f"progress={received}/{total} firmware=0x{firmware:08x}"
            )
        else:
            package = args.package.read_bytes()
            if len(package) > 1024 * 1024:
                raise ValueError("package exceeds the 1 MiB OTA slot")
            firmware, image_size = inspect_package(package)
            print(
                f"package firmware=0x{firmware:08x}, "
                f"payload={image_size} bytes"
            )
            client.upload(package, args.reboot, args.chunk_size)
    finally:
        client.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (ConnectionError, OSError, RuntimeError, ValueError) as error:
        print(f"error: {error}", file=sys.stderr)
        raise SystemExit(1)
