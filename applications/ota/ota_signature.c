#include "ota.h"

#include <string.h>

#include "ota_config.h"
#include "ota_hash.h"
#include "ota_trusted_key.h"
#include "micro-ecc/uECC.h"

#define OTA_SIGNATURE_DOMAIN "H743OTA1"
#define OTA_SIGNATURE_DOMAIN_SIZE 8U

static void store_le16(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
}

static void store_le32(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)value;
    output[1] = (uint8_t)(value >> 8U);
    output[2] = (uint8_t)(value >> 16U);
    output[3] = (uint8_t)(value >> 24U);
}

static void build_signed_data(
    const boot_image_header_t *header,
    const uint8_t image_digest[BOOT_SHA256_DIGEST_SIZE],
    uint8_t output[BOOT_IMAGE_SIGNED_DATA_SIZE])
{
    uint8_t *cursor = output;

    memcpy(cursor, OTA_SIGNATURE_DOMAIN, OTA_SIGNATURE_DOMAIN_SIZE);
    cursor += OTA_SIGNATURE_DOMAIN_SIZE;
    store_le32(cursor, header->magic); cursor += 4U;
    store_le16(cursor, header->format_version); cursor += 2U;
    store_le16(cursor, header->header_size); cursor += 2U;
    store_le32(cursor, header->flags); cursor += 4U;
    store_le32(cursor, header->hardware_id); cursor += 4U;
    store_le32(cursor, header->firmware_version); cursor += 4U;
    store_le32(cursor, header->payload_offset); cursor += 4U;
    store_le32(cursor, header->image_size); cursor += 4U;
    store_le32(cursor, header->load_address); cursor += 4U;
    store_le32(cursor, header->image_crc32); cursor += 4U;
    memcpy(cursor, image_digest, BOOT_SHA256_DIGEST_SIZE);
    cursor += BOOT_SHA256_DIGEST_SIZE;
    store_le16(cursor, header->signature_algorithm); cursor += 2U;
    store_le16(cursor, header->signature_size);
}

int ota_signature_verify(
    const boot_image_header_t *header,
    const uint8_t image_digest[BOOT_SHA256_DIGEST_SIZE])
{
    uint8_t signed_data[BOOT_IMAGE_SIGNED_DATA_SIZE];
    uint8_t signed_digest[BOOT_SHA256_DIGEST_SIZE];
    ota_sha256_context_t sha;

    if ((header == NULL) || (image_digest == NULL))
    {
        return 0;
    }
    if (header->signature_algorithm == BOOT_SIGNATURE_NONE)
    {
        return OTA_ALLOW_UNSIGNED_IMAGES && (header->signature_size == 0U);
    }
    if ((header->signature_algorithm != BOOT_SIGNATURE_ECDSA_P256_RAW) ||
        (header->signature_size != BOOT_IMAGE_SIGNATURE_SIZE))
    {
        return 0;
    }

    build_signed_data(header, image_digest, signed_data);
    ota_sha256_init(&sha);
    ota_sha256_update(&sha, signed_data, sizeof(signed_data));
    ota_sha256_finish(&sha, signed_digest);
    return uECC_verify(ota_trusted_public_key,
                       signed_digest,
                       sizeof(signed_digest),
                       header->signature,
                       uECC_secp256r1());
}
