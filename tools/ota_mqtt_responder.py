#!/usr/bin/env python3
"""Minimal MQTT 3.1.1 responder for local H743 OTA validation."""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import socket
import struct
import time
import urllib.parse
import zlib


PROTOCOL = "ota-v1"
HARDWARE_ID = 0x48373433


def encode_remaining_length(value: int) -> bytes:
    encoded = bytearray()
    while True:
        digit = value % 128
        value //= 128
        if value:
            digit |= 0x80
        encoded.append(digit)
        if not value:
            return bytes(encoded)


def mqtt_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    return struct.pack("!H", len(encoded)) + encoded


def make_packet(header: int, body: bytes) -> bytes:
    return bytes((header,)) + encode_remaining_length(len(body)) + body


def read_exact(connection: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = connection.recv(size - len(data))
        if not chunk:
            raise ConnectionError("MQTT broker closed the connection")
        data.extend(chunk)
    return bytes(data)


def receive_packet(connection: socket.socket) -> tuple[int, bytes]:
    first = read_exact(connection, 1)[0]
    multiplier = 1
    remaining = 0
    for _ in range(4):
        digit = read_exact(connection, 1)[0]
        remaining += (digit & 0x7F) * multiplier
        if not digit & 0x80:
            return first, read_exact(connection, remaining)
        multiplier *= 128
    raise ValueError("invalid MQTT remaining length")


def parse_c_string_define(text: str, name: str) -> str:
    match = re.search(
        rf"^\s*#define\s+{re.escape(name)}\s+\"((?:\\.|[^\"\\])*)\"",
        text,
        re.MULTILINE,
    )
    if not match:
        raise ValueError(f"missing string macro {name}")
    return bytes(match.group(1), "utf-8").decode("unicode_escape")


def load_connection_config(path: pathlib.Path) -> tuple[str, str, str]:
    text = path.read_text(encoding="utf-8")
    return (
        parse_c_string_define(text, "OTA_MQTT_BROKER_URI"),
        parse_c_string_define(text, "OTA_MQTT_USERNAME"),
        parse_c_string_define(text, "OTA_MQTT_PASSWORD"),
    )


class MqttClient:
    def __init__(self, host: str, port: int, username: str, password: str):
        self.host = host
        self.port = port
        self.username = username
        self.password = password
        self.connection: socket.socket | None = None
        self.packet_id = 1

    def next_packet_id(self) -> int:
        value = self.packet_id
        self.packet_id = 1 if value == 0xFFFF else value + 1
        return value

    def connect(self) -> None:
        connection = socket.create_connection((self.host, self.port), 10)
        connection.settimeout(30)
        flags = 0x02
        payload = mqtt_string(f"ota-validation-{int(time.time()):x}")
        if self.username:
            flags |= 0x80
            payload += mqtt_string(self.username)
        if self.password:
            flags |= 0x40
            payload += mqtt_string(self.password)
        variable = mqtt_string("MQTT") + bytes((4, flags)) + struct.pack("!H", 30)
        connection.sendall(make_packet(0x10, variable + payload))
        header, body = receive_packet(connection)
        if header >> 4 != 2 or len(body) != 2 or body[1] != 0:
            raise ConnectionError(f"MQTT CONNACK rejected: {body.hex()}")
        self.connection = connection

    def send(self, data: bytes) -> None:
        if self.connection is None:
            raise ConnectionError("MQTT client is not connected")
        self.connection.sendall(data)

    def subscribe(self, topics: list[str]) -> None:
        packet_id = self.next_packet_id()
        body = struct.pack("!H", packet_id)
        for topic in topics:
            body += mqtt_string(topic) + bytes((1,))
        self.send(make_packet(0x82, body))

    def publish(self, topic: str, payload: bytes) -> None:
        packet_id = self.next_packet_id()
        body = mqtt_string(topic) + struct.pack("!H", packet_id) + payload
        self.send(make_packet(0x32, body))

    def acknowledge(self, packet_id: int) -> None:
        self.send(make_packet(0x40, struct.pack("!H", packet_id)))

    def ping(self) -> None:
        self.send(bytes((0xC0, 0x00)))


def parse_publish(header: int, body: bytes) -> tuple[str, bytes, int | None]:
    if len(body) < 2:
        raise ValueError("short MQTT PUBLISH packet")
    topic_length = struct.unpack_from("!H", body)[0]
    cursor = 2
    end = cursor + topic_length
    topic = body[cursor:end].decode("utf-8")
    cursor = end
    packet_id = None
    qos = (header >> 1) & 0x03
    if qos:
        packet_id = struct.unpack_from("!H", body, cursor)[0]
        cursor += 2
    return topic, body[cursor:], packet_id


def build_response(
    request: dict[str, object],
    package: bytes,
    version: str,
    version_code: int,
    download_url: str,
) -> dict[str, object]:
    current_code = int(request.get("current_version_code", 0))
    update_available = current_code < version_code
    firmware: dict[str, object] = {
        "version": version,
        "version_code": version_code,
    }
    if update_available:
        firmware.update(
            {
                "package_size": len(package),
                "package_crc32": f"{zlib.crc32(package) & 0xFFFFFFFF:08X}",
                "package_sha256": hashlib.sha256(package).hexdigest(),
                "download_url": download_url,
                "release_notes": "Local end-to-end OTA validation build",
            }
        )
    return {
        "protocol": PROTOCOL,
        "type": "version_response",
        "message_id": f"server-{time.time_ns():x}"[-47:],
        "request_message_id": request["message_id"],
        "device_id": request["device_id"],
        "hardware_id": int(request["hardware_id"]),
        "update_available": update_available,
        "mandatory": False,
        "rollout_delay_max_seconds": 0,
        "firmware": firmware,
    }


def parse_args() -> argparse.Namespace:
    root = pathlib.Path(__file__).resolve().parents[1]
    public_config = root / "applications/ota/ota_config.h"
    local_config = root / "applications/ota/ota_config_local.h"
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--config",
        type=pathlib.Path,
        default=local_config if local_config.exists() else public_config,
    )
    parser.add_argument(
        "--package", type=pathlib.Path, default=root / "Release/h743_V1.0.1.fwpkg"
    )
    parser.add_argument("--url", help="device-visible HTTP package URL")
    parser.add_argument("--version", default="V1.0.1")
    parser.add_argument("--version-code", type=lambda value: int(value, 0), default=0x10001)
    parser.add_argument(
        "--exit-on-current",
        action="store_true",
        help="exit after the upgraded device requests its current version",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    broker_uri, username, password = load_connection_config(args.config)
    parsed = urllib.parse.urlparse(broker_uri)
    if parsed.scheme != "tcp" or not parsed.hostname:
        raise SystemExit(f"unsupported MQTT broker URI: {broker_uri}")
    package = args.package.read_bytes()
    port = parsed.port or 1883
    download_url = args.url or (
        f"http://{parsed.hostname}:8000/firmware/{args.package.name}"
    )
    client = MqttClient(parsed.hostname, port, username, password)
    client.connect()
    client.subscribe(
        [
            "ota/v1/device/+/version/request",
            "ota/v1/device/+/update/event",
            "ota/v1/device/+/status",
        ]
    )
    print(f"MQTT connected: {parsed.hostname}:{port}", flush=True)
    print(f"Serving {args.version} ({args.version_code:#010x}) at {download_url}", flush=True)
    print(
        f"Package: {len(package)} bytes, CRC32={zlib.crc32(package) & 0xFFFFFFFF:08X}, "
        f"SHA256={hashlib.sha256(package).hexdigest()}",
        flush=True,
    )

    while True:
        try:
            if client.connection is None:
                raise ConnectionError("MQTT client is not connected")
            header, body = receive_packet(client.connection)
        except socket.timeout:
            client.ping()
            continue
        packet_type = header >> 4
        if packet_type != 3:
            continue
        topic, payload, packet_id = parse_publish(header, body)
        if packet_id is not None:
            client.acknowledge(packet_id)
        try:
            message = json.loads(payload.decode("utf-8"))
        except (UnicodeDecodeError, json.JSONDecodeError) as error:
            print(f"Ignoring invalid JSON on {topic}: {error}", flush=True)
            continue

        if topic.endswith("/version/request"):
            if message.get("protocol") != PROTOCOL or message.get("type") != "version_request":
                continue
            if int(message.get("hardware_id", 0)) != HARDWARE_ID:
                print(f"Ignoring unexpected hardware ID on {topic}", flush=True)
                continue
            response = build_response(
                message, package, args.version, args.version_code, download_url
            )
            response_topic = topic.removesuffix("/request") + "/response"
            encoded = json.dumps(response, separators=(",", ":")).encode("utf-8")
            client.publish(response_topic, encoded)
            print(
                f"Version request from {message['device_id']}: "
                f"current={message.get('current_version')} "
                f"({int(message.get('current_version_code', 0)):#010x}), "
                f"update={response['update_available']}",
                flush=True,
            )
            if args.exit_on_current and not response["update_available"]:
                print("OTA validation complete: device reports target version", flush=True)
                return 0
        elif topic.endswith("/update/event"):
            print(
                "OTA event: "
                f"{message.get('event')} stage={message.get('stage')} "
                f"progress={message.get('progress_percent')}% "
                f"error={message.get('error_code')}",
                flush=True,
            )
        elif topic.endswith("/status"):
            print(
                f"Device status: online={message.get('online')} "
                f"version={message.get('current_version')} "
                f"code={int(message.get('current_version_code', 0)):#010x}",
                flush=True,
            )


if __name__ == "__main__":
    raise SystemExit(main())
