#include "ota_http.h"

#include <ctype.h>
#include <rtthread.h>
#include <string.h>
#include <webclient.h>

#include "ota.h"
#include "ota_config.h"
#include "ota_hash.h"

#define DBG_TAG "app.ota.http"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

static int hex_nibble(char character)
{
    if ((character >= '0') && (character <= '9'))
    {
        return character - '0';
    }
    character = (char)tolower((unsigned char)character);
    if ((character >= 'a') && (character <= 'f'))
    {
        return character - 'a' + 10;
    }
    return -1;
}

static int parse_sha256(const char *text,
                        uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    size_t index;

    if ((text == RT_NULL) ||
        (strlen(text) != BOOT_SHA256_DIGEST_SIZE * 2U))
    {
        return OTA_ERROR_PROTOCOL;
    }
    for (index = 0U; index < BOOT_SHA256_DIGEST_SIZE; ++index)
    {
        int high = hex_nibble(text[index * 2U]);
        int low = hex_nibble(text[index * 2U + 1U]);
        if ((high < 0) || (low < 0))
        {
            return OTA_ERROR_PROTOCOL;
        }
        digest[index] = (uint8_t)((high << 4) | low);
    }
    return OTA_OK;
}

static int digest_equal(const uint8_t *left,
                        const uint8_t *right,
                        size_t length)
{
    uint8_t difference = 0U;
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static int notify_progress(ota_http_progress_cb_t callback,
                           void *user_ctx,
                           ota_http_stage_t stage,
                           uint32_t received,
                           uint32_t total,
                           uint8_t percent)
{
    if (callback != RT_NULL)
    {
        int result = callback(user_ctx, stage, received, total, percent);
        if (result != 0)
        {
            return result < 0 ? result : OTA_ERROR_CANCELLED;
        }
    }
    return OTA_OK;
}

int ota_http_download(const ota_http_request_t *request,
                      ota_http_progress_cb_t progress,
                      void *user_ctx)
{
    struct webclient_session *session = RT_NULL;
    uint8_t *buffer = RT_NULL;
    ota_sha256_context_t sha;
    uint8_t expected_sha[BOOT_SHA256_DIGEST_SIZE];
    uint8_t actual_sha[BOOT_SHA256_DIGEST_SIZE];
    uint32_t received = 0U;
    uint8_t last_percent = 0U;
    int begun = 0;
    int result = OTA_ERROR_NETWORK;
    int content_length;
    int response_status;

    if ((request == RT_NULL) || (request->url == RT_NULL) ||
        (request->package_sha256 == RT_NULL) ||
        (request->package_size < BOOT_IMAGE_HEADER_SIZE) ||
        (request->package_size > OTA_SLOT_SIZE))
    {
        return OTA_ERROR_ARGUMENT;
    }
    if (strncmp(request->url, "https://", 8U) == 0)
    {
        return OTA_ERROR_UNSUPPORTED;
    }
    if (strncmp(request->url, "http://", 7U) != 0)
    {
        return OTA_ERROR_PROTOCOL;
    }
    result = parse_sha256(request->package_sha256, expected_sha);
    if (result != OTA_OK)
    {
        return result;
    }

    buffer = (uint8_t *)web_malloc(OTA_HTTP_BUFFER_SIZE);
    session = webclient_session_create(OTA_HTTP_HEADER_SIZE);
    if ((buffer == RT_NULL) || (session == RT_NULL))
    {
        result = OTA_ERROR_NO_MEMORY;
        goto exit;
    }
    webclient_set_timeout(session, OTA_HTTP_TIMEOUT_MS);
    response_status = webclient_get(session, request->url);
    if (response_status != 200)
    {
        LOG_E("GET failed, HTTP status=%d", response_status);
        result = OTA_ERROR_NETWORK;
        goto exit;
    }
    content_length = webclient_content_length_get(session);
    if ((content_length < 0) ||
        ((uint32_t)content_length != request->package_size))
    {
        LOG_E("Content-Length mismatch: expected=%u actual=%d",
              (unsigned)request->package_size, content_length);
        result = OTA_ERROR_SIZE;
        goto exit;
    }

    result = ota_begin("http", request->package_size,
                       request->package_crc32);
    if (result != OTA_OK)
    {
        goto exit;
    }
    begun = 1;
    ota_sha256_init(&sha);
    result = notify_progress(progress, user_ctx, OTA_HTTP_STAGE_DOWNLOAD,
                             0U, request->package_size, 0U);
    if (result != OTA_OK)
    {
        goto exit;
    }

    while (received < request->package_size)
    {
        uint32_t remaining = request->package_size - received;
        size_t chunk = remaining < OTA_HTTP_BUFFER_SIZE
                           ? remaining
                           : OTA_HTTP_BUFFER_SIZE;
        int bytes_read = webclient_read(session, buffer, chunk);
        uint8_t percent;

        if (bytes_read <= 0)
        {
            result = OTA_ERROR_NETWORK;
            goto exit;
        }
        if ((uint32_t)bytes_read > remaining)
        {
            result = OTA_ERROR_SIZE;
            goto exit;
        }
        ota_sha256_update(&sha, buffer, (size_t)bytes_read);
        result = ota_write(received, buffer, (size_t)bytes_read);
        if (result != OTA_OK)
        {
            goto exit;
        }
        received += (uint32_t)bytes_read;
        percent = (uint8_t)(((uint64_t)received * 100U) /
                            request->package_size);
        if ((percent == 100U) ||
            (percent >= (uint8_t)(last_percent + OTA_HTTP_PROGRESS_STEP)))
        {
            last_percent = percent;
            result = notify_progress(progress, user_ctx,
                                     OTA_HTTP_STAGE_DOWNLOAD,
                                     received, request->package_size,
                                     percent);
            if (result != OTA_OK)
            {
                goto exit;
            }
        }
    }

    result = notify_progress(progress, user_ctx, OTA_HTTP_STAGE_VERIFY,
                             received, request->package_size, 100U);
    if (result != OTA_OK)
    {
        goto exit;
    }
    ota_sha256_finish(&sha, actual_sha);
    if (!digest_equal(actual_sha, expected_sha, sizeof(actual_sha)))
    {
        result = OTA_ERROR_PACKAGE_SHA256;
        goto exit;
    }
    result = ota_finish();
    if (result != OTA_OK)
    {
        goto exit;
    }
    begun = 0;
    result = notify_progress(progress, user_ctx, OTA_HTTP_STAGE_COMMIT,
                             received, request->package_size, 100U);

exit:
    if (begun)
    {
        ota_abort();
    }
    if (session != RT_NULL)
    {
        webclient_close(session);
    }
    if (buffer != RT_NULL)
    {
        web_free(buffer);
    }
    return result;
}
