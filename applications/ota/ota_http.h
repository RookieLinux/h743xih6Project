#ifndef APP_OTA_HTTP_H
#define APP_OTA_HTTP_H

#include <stdint.h>

#include "ota_mqtt_protocol.h"

typedef enum
{
    OTA_HTTP_STAGE_DOWNLOAD = 0,
    OTA_HTTP_STAGE_VERIFY,
    OTA_HTTP_STAGE_COMMIT
} ota_http_stage_t;

typedef struct
{
    const char *url;
    uint32_t package_size;
    uint32_t package_crc32;
    const char *package_sha256;
} ota_http_request_t;

/*
 * Return non-zero from progress to cancel. The callback runs in the caller's
 * worker thread and must not call ota_http_download() recursively.
 */
typedef int (*ota_http_progress_cb_t)(void *user_ctx,
                                     ota_http_stage_t stage,
                                     uint32_t received_size,
                                     uint32_t package_size,
                                     uint8_t progress_percent);

int ota_http_download(const ota_http_request_t *request,
                      ota_http_progress_cb_t progress,
                      void *user_ctx);

#endif
