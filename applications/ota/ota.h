#ifndef APP_OTA_H
#define APP_OTA_H

#include <stddef.h>
#include <stdint.h>

#include "boot_image.h"

typedef enum
{
    OTA_STATE_UNINITIALIZED = 0,
    OTA_STATE_IDLE,
    OTA_STATE_RECEIVING,
    OTA_STATE_VALIDATING,
    OTA_STATE_COMMITTING,
    OTA_STATE_READY,
    OTA_STATE_ERROR
} ota_state_t;

typedef enum
{
    OTA_OK = 0,
    OTA_ERROR_ARGUMENT = -1,
    OTA_ERROR_STATE = -2,
    OTA_ERROR_PARTITION = -3,
    OTA_ERROR_SIZE = -4,
    OTA_ERROR_FLASH = -5,
    OTA_ERROR_TRANSFER_CRC = -6,
    OTA_ERROR_HEADER = -7,
    OTA_ERROR_IMAGE_CRC = -8,
    OTA_ERROR_IMAGE_SHA256 = -9,
    OTA_ERROR_SIGNATURE = -10,
    OTA_ERROR_NO_MEMORY = -11,
    OTA_ERROR_NETWORK = -12,
    OTA_ERROR_PROTOCOL = -13,
    OTA_ERROR_TIMEOUT = -14,
    OTA_ERROR_PACKAGE_SHA256 = -15,
    OTA_ERROR_UNSUPPORTED = -16,
    OTA_ERROR_CANCELLED = -17
} ota_result_t;

typedef struct
{
    ota_state_t state;
    int last_error;
    uint32_t package_size;
    uint32_t received_size;
    uint32_t firmware_version;
    char source[16];
} ota_status_t;

/*
 * Transport-neutral producer API. A UART/USB/HTTP transport only needs to
 * feed a complete .fwpkg sequentially through begin/write/finish.
 */
int ota_service_init(void);
int ota_begin(const char *source,
              uint32_t package_size,
              uint32_t package_crc32);
int ota_write(uint32_t offset, const void *data, size_t length);
int ota_finish(void);
void ota_abort(void);
void ota_get_status(ota_status_t *status);
const char *ota_result_string(int result);

/* Reboot after a successful ota_finish(), so Bootloader installs upgrade. */
int ota_reboot_to_install(uint32_t delay_ms);

/*
 * Override this weak function when Bootloader signed-image verification is
 * enabled. Its policy and public key must match boot_signature_verify().
 */
int ota_signature_verify(
    const boot_image_header_t *header,
    const uint8_t image_digest[BOOT_SHA256_DIGEST_SIZE]);

#endif
