# APP OTA Design and Wi-Fi Host Interface

## 1. OTA Data Flow

```text
Host .fwpkg
    |  Wi-Fi / TCP :5000
    v
download partition (write package body first, header last)
    |  Package CRC + header CRC + payload CRC32/SHA-256/signature policy
    v
upgrade partition (erase and copy payload first, verify it, then write header last)
    |  APP-initiated reset
    v
Bootloader verifies upgrade -> installs it into the internal app partition
```

Both the `download` and `upgrade` partitions use the firmware package format
defined in `h743xih6Bootloader/bootloader/boot_image.h`. The APP-side
`boot_image.h` mirrors this wire-format definition. If the Bootloader format
changes, the APP definition must be updated accordingly and both projects must
be rebuilt.

Power-loss safety strategy:

- At the beginning of a download, the required sectors in `download` are
  erased. During transmission, the first 256-byte package header is kept in
  RAM, while the package body is written directly to Flash.
- The package header is written to `download` only after the complete package
  has been received and has passed every validation step.
- The `upgrade` partition is erased first. The payload is then copied and
  verified before the package header is written.
- Consequently, a power interruption can leave only an incomplete package
  without a valid header, which the Bootloader will not recognize as an
  upgrade image.
- If the `upgrade` header has already been committed, the Bootloader can
  continue the installation at the next power-on. A failed internal APP
  installation is still handled by the existing `factory` fallback logic.

## 2. Extensible Transport Interface

The core API is declared in `ota.h` and is independent of Wi-Fi:

```c
ota_begin("usb", package_size, package_crc32);
ota_write(offset, data, length);  /* Must be written sequentially from offset 0. */
ota_finish();                     /* Validate download and atomically commit upgrade. */
ota_reboot_to_install(500);
```

To add UART/YMODEM, USB CDC, USB mass storage, HTTP/HTTPS pull, BLE, or another
transport later, implement an adapter that obtains the total package length
and package CRC, then calls `begin/write/finish` sequentially. A transport
adapter must not operate on the `download` or `upgrade` partitions directly.

The APP and Bootloader now perform independent ECDSA P-256 verification with
the same trusted public key. The 64-byte big-endian `r || s` signature covers
a domain separator and immutable metadata including the hardware ID, firmware
version, payload size, load address, CRC32, and SHA-256. Unsigned images are
rejected by default. `OTA_ALLOW_UNSIGNED_IMAGES=1` is only for a controlled
migration from legacy firmware and must not be used in production.

For an existing device, the legacy APP and Bootloader cannot understand a
signed package. Package the new signature-capable APP once with
`mkimage.py --unsigned`. After that bridge APP starts, it is strict by default
and accepts only signed updates. Provision a signed factory package and the
strict Bootloader next. Do not use `--unsigned` for normal releases.

Run `tools/ota_signing_key.py` once for development to create the local private
key and `applications/ota/ota_trusted_key.h` in the APP project. The script
does not modify the Bootloader project; manually copy that header to
`bootloader/ota_trusted_key.h` when updating the Bootloader trust key. The
private-key path is ignored by Git. Replace the development key with an offline
or HSM-managed production key before manufacturing.

## 3. Wi-Fi TCP Protocol v1

The device acts as a TCP server. After joining the existing RW007 STA network,
it listens on `0.0.0.0:5000`. The host acts as the TCP client. All multi-byte
integers use little-endian byte order.

Each frame starts with a fixed 24-byte header:

| Offset | Type | Field | Description |
|---:|---|---|---|
| 0 | u32 | magic | `0x3141544F`, represented as `OTA1` in memory |
| 4 | u16 | version | `1` |
| 6 | u16 | type | Request type; a response uses `type \| 0x8000` |
| 8 | u32 | sequence | Request sequence number, echoed by the response |
| 12 | u32 | offset/status | Package offset for DATA; signed result code in a response |
| 16 | u32 | payload_length | Number of following payload bytes, maximum 4096 |
| 20 | u32 | payload_crc32 | IEEE CRC32 of the payload; zero for an empty payload |

Request types:

| type | Name | Payload / behavior |
|---:|---|---|
| 1 | QUERY | Empty; response contains a 20-byte status payload |
| 2 | BEGIN | 12 bytes: `package_size, package_crc32, flags` |
| 3 | DATA | Firmware package data; `offset` must equal the current received length |
| 4 | END | Empty; performs complete validation and copies the image to `upgrade` |
| 5 | ABORT | Empty; invalidates the incomplete package in `download` |
| 6 | REBOOT | Empty; accepted only in the READY state, then resets into the Bootloader |

Bit 0 of BEGIN `flags` requests an automatic reset after END succeeds and its
response has been sent. The QUERY response payload is:

```text
u32 state
i32 last_error
u32 package_size
u32 received_size
u32 firmware_version
```

Recommended exchange:

1. Establish the TCP connection and send QUERY to confirm that the device is
   not busy.
2. Calculate the complete `.fwpkg` file length and IEEE CRC32, then send BEGIN.
3. After receiving a successful response, send DATA frames containing no more
   than 4096 bytes each. Wait for the ACK of each frame before sending the
   next frame.
4. Send END. Flash verification and copying can take some time, so the host
   should use a timeout of at least 120 seconds and continue displaying a
   "device validating/committing" state.
5. After END succeeds, send REBOOT when needed, or set the automatic-reset
   flag in BEGIN.
6. If the connection closes unexpectedly, the device automatically calls
   ABORT, so an incomplete package is never committed.

## 4. Host Application Architecture

The host application should expose the following interfaces so that the GUI
does not operate on sockets directly:

```text
PackageService
  +-- inspect(path): parse boot_image_header_t and show hardware ID/version/size
  +-- validate(path): locally verify header CRC, payload CRC32, and SHA-256

DeviceTransport
  +-- connect(endpoint)
  +-- request(frame)
  +-- close()
       +-- TcpOtaTransport (current)
           Future: SerialOtaTransport / UsbOtaTransport

OtaSession
  +-- query()
  +-- upload(path, progress_callback, cancel_token)
  +-- abort()
  +-- reboot()
```

At minimum, the GUI should display the device IP address, connection state,
current and target versions, hardware ID, transmitted byte count, current
phase (erase/transfer/validate/commit/waiting for restart), device error code,
and a retry action. Protocol v1 can begin with manual IP entry. UDP broadcast
or mDNS discovery may be added later without changing the OTA TCP frame
protocol.

ECDSA verification now authenticates the firmware end to end. The current
plain TCP, MQTT, and HTTP transports still provide no link confidentiality.
Production deployments should additionally use MQTT TLS, HTTPS, per-device
credentials, and Broker ACLs. Anti-rollback version policies should be added
without changing the meaning of protocol v1 fields.
