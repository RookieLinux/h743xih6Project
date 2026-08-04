#ifndef APP_BOOT_IMAGE_H
#define APP_BOOT_IMAGE_H

#include <stdint.h>

/*
 * Keep this wire-format definition in sync with
 * h743xih6Bootloader/bootloader/boot_image.h.
 */
#define BOOT_SHA256_DIGEST_SIZE    32U
#define BOOT_IMAGE_MAGIC           0x544F4F42UL
#define BOOT_IMAGE_FORMAT_VERSION  1U
#define BOOT_IMAGE_HEADER_SIZE     256U
#define BOOT_IMAGE_FLAG_VALID      (1UL << 0)

typedef enum
{
    BOOT_SIGNATURE_NONE = 0,
    BOOT_SIGNATURE_ECDSA_P256_RAW = 1,
    BOOT_SIGNATURE_ED25519 = 2
} boot_signature_algorithm_t;

typedef struct
{
    uint32_t magic;
    uint16_t format_version;
    uint16_t header_size;
    uint32_t flags;
    uint32_t hardware_id;
    uint32_t firmware_version;
    uint32_t payload_offset;
    uint32_t image_size;
    uint32_t load_address;
    uint32_t image_crc32;
    uint8_t image_sha256[BOOT_SHA256_DIGEST_SIZE];
    uint16_t signature_algorithm;
    uint16_t signature_size;
    uint8_t signature[64];
    uint8_t reserved[116];
    uint32_t header_crc32;
} boot_image_header_t;

typedef char app_boot_image_header_size_must_be_256[
    sizeof(boot_image_header_t) == BOOT_IMAGE_HEADER_SIZE ? 1 : -1];

#endif
