#ifndef APP_OTA_HASH_H
#define APP_OTA_HASH_H

#include <stddef.h>
#include <stdint.h>

#include "boot_image.h"

typedef struct
{
    uint32_t value;
} ota_crc32_context_t;

typedef struct
{
    uint32_t state[8];
    uint64_t bit_length;
    uint32_t buffer_length;
    uint8_t buffer[64];
} ota_sha256_context_t;

void ota_crc32_init(ota_crc32_context_t *context);
void ota_crc32_update(ota_crc32_context_t *context,
                      const void *data,
                      size_t length);
uint32_t ota_crc32_finish(const ota_crc32_context_t *context);
uint32_t ota_crc32_calculate(const void *data, size_t length);

void ota_sha256_init(ota_sha256_context_t *context);
void ota_sha256_update(ota_sha256_context_t *context,
                       const void *data,
                       size_t length);
void ota_sha256_finish(ota_sha256_context_t *context,
                       uint8_t digest[BOOT_SHA256_DIGEST_SIZE]);

#endif
