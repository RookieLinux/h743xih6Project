#include "ota_mqtt_protocol.h"

#include <ctype.h>
#include <cJSON.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ota.h"
#include "ota_config.h"

static int copy_json_string(const cJSON *object,
                            const char *name,
                            char *output,
                            size_t output_size,
                            int required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    size_t length;

    if (item == NULL)
    {
        if (required)
        {
            return OTA_ERROR_PROTOCOL;
        }
        output[0] = '\0';
        return OTA_OK;
    }
    if (!cJSON_IsString(item) || (item->valuestring == NULL))
    {
        return OTA_ERROR_PROTOCOL;
    }
    length = strlen(item->valuestring);
    if (length >= output_size)
    {
        return OTA_ERROR_SIZE;
    }
    memcpy(output, item->valuestring, length + 1U);
    return OTA_OK;
}

static int get_uint32(const cJSON *object,
                      const char *name,
                      uint32_t *value,
                      int required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);
    double number;

    if (item == NULL)
    {
        return required ? OTA_ERROR_PROTOCOL : OTA_OK;
    }
    if (!cJSON_IsNumber(item))
    {
        return OTA_ERROR_PROTOCOL;
    }
    number = item->valuedouble;
    if ((number < 0.0) || (number > 4294967295.0) ||
        (number != (double)(uint32_t)number))
    {
        return OTA_ERROR_PROTOCOL;
    }
    *value = (uint32_t)number;
    return OTA_OK;
}

static int get_bool(const cJSON *object,
                    const char *name,
                    uint8_t *value,
                    int required)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(object, name);

    if (item == NULL)
    {
        return required ? OTA_ERROR_PROTOCOL : OTA_OK;
    }
    if (!cJSON_IsBool(item))
    {
        return OTA_ERROR_PROTOCOL;
    }
    *value = cJSON_IsTrue(item) ? 1U : 0U;
    return OTA_OK;
}

static int is_hex_string(const char *text, size_t length)
{
    size_t index;

    if ((text == NULL) || (strlen(text) != length))
    {
        return 0;
    }
    for (index = 0U; index < length; ++index)
    {
        if (!isxdigit((unsigned char)text[index]))
        {
            return 0;
        }
    }
    return 1;
}

static int parse_crc32(const cJSON *firmware, uint32_t *crc)
{
    const cJSON *item = cJSON_GetObjectItemCaseSensitive(
        firmware, "package_crc32");
    char *end = NULL;
    unsigned long value;

    if ((item == NULL) || !cJSON_IsString(item) ||
        !is_hex_string(item->valuestring, 8U))
    {
        return OTA_ERROR_PROTOCOL;
    }
    value = strtoul(item->valuestring, &end, 16);
    if ((end == NULL) || (*end != '\0'))
    {
        return OTA_ERROR_PROTOCOL;
    }
    *crc = (uint32_t)value;
    return OTA_OK;
}

static int add_string(cJSON *object, const char *name, const char *value)
{
    return cJSON_AddStringToObject(object, name, value) != NULL;
}

static int print_json(cJSON *root, char *json, size_t json_size)
{
    int result;

    if ((json == NULL) || (json_size < 8U))
    {
        cJSON_Delete(root);
        return OTA_ERROR_ARGUMENT;
    }
    result = cJSON_PrintPreallocated(root, json, (int)json_size, 0);
    cJSON_Delete(root);
    return result ? OTA_OK : OTA_ERROR_SIZE;
}

const char *ota_mqtt_event_name(ota_mqtt_event_type_t event)
{
    static const char *const names[] =
    {
        "accepted",
        "declined",
        "download_started",
        "download_progress",
        "verifying",
        "committed",
        "rebooting",
        "completed",
        "failed"
    };

    if ((unsigned)event >= (sizeof(names) / sizeof(names[0])))
    {
        return "failed";
    }
    return names[event];
}

int ota_mqtt_build_version_request(
    const ota_mqtt_version_request_t *request,
    char *json,
    size_t json_size)
{
    cJSON *root;

    if ((request == NULL) || (request->message_id[0] == '\0') ||
        (request->device_id[0] == '\0'))
    {
        return OTA_ERROR_ARGUMENT;
    }
    root = cJSON_CreateObject();
    if ((root == NULL) ||
        !add_string(root, "protocol", OTA_MQTT_PROTOCOL_NAME) ||
        !add_string(root, "type", "version_request") ||
        !add_string(root, "message_id", request->message_id) ||
        !add_string(root, "device_id", request->device_id) ||
        (cJSON_AddNumberToObject(root, "hardware_id",
                                 request->hardware_id) == NULL) ||
        !add_string(root, "current_version", request->current_version) ||
        (cJSON_AddNumberToObject(root, "current_version_code",
                                 request->current_version_code) == NULL) ||
        !add_string(root, "bootloader_version",
                    request->bootloader_version))
    {
        cJSON_Delete(root);
        return OTA_ERROR_NO_MEMORY;
    }
    return print_json(root, json, json_size);
}

int ota_mqtt_parse_version_response(
    const char *json,
    size_t json_length,
    ota_mqtt_version_response_t *response)
{
    cJSON *root = NULL;
    const cJSON *firmware;
    char protocol[16];
    char type[24];
    int result = OTA_ERROR_PROTOCOL;

    if ((json == NULL) || (json_length == 0U) || (response == NULL))
    {
        return OTA_ERROR_ARGUMENT;
    }
    memset(response, 0, sizeof(*response));
    root = cJSON_ParseWithLength(json, json_length);
    if (!cJSON_IsObject(root))
    {
        goto exit;
    }
    if ((copy_json_string(root, "protocol", protocol,
                          sizeof(protocol), 1) != OTA_OK) ||
        (copy_json_string(root, "type", type, sizeof(type), 1) != OTA_OK) ||
        (strcmp(protocol, OTA_MQTT_PROTOCOL_NAME) != 0) ||
        (strcmp(type, "version_response") != 0) ||
        (copy_json_string(root, "message_id", response->message_id,
                          sizeof(response->message_id), 1) != OTA_OK) ||
        (copy_json_string(root, "request_message_id",
                          response->request_message_id,
                          sizeof(response->request_message_id), 1) != OTA_OK) ||
        (copy_json_string(root, "device_id", response->device_id,
                          sizeof(response->device_id), 1) != OTA_OK) ||
        (get_uint32(root, "hardware_id", &response->hardware_id, 1) !=
         OTA_OK) ||
        (get_bool(root, "update_available",
                  &response->update_available, 1) != OTA_OK) ||
        (get_bool(root, "mandatory", &response->mandatory, 0) != OTA_OK) ||
        (get_uint32(root, "rollout_delay_max_seconds",
                    &response->rollout_delay_max_seconds, 0) != OTA_OK) ||
        (response->rollout_delay_max_seconds >
         OTA_MQTT_ROLLOUT_DELAY_LIMIT))
    {
        goto exit;
    }

    firmware = cJSON_GetObjectItemCaseSensitive(root, "firmware");
    if (!cJSON_IsObject(firmware) ||
        (copy_json_string(firmware, "version", response->version,
                          sizeof(response->version), 1) != OTA_OK) ||
        (get_uint32(firmware, "version_code",
                    &response->version_code, 1) != OTA_OK))
    {
        goto exit;
    }
    if (!response->update_available)
    {
        result = OTA_OK;
        goto exit;
    }
    if ((get_uint32(firmware, "package_size",
                    &response->package_size, 1) != OTA_OK) ||
        (parse_crc32(firmware, &response->package_crc32) != OTA_OK) ||
        (copy_json_string(firmware, "package_sha256",
                          response->package_sha256,
                          sizeof(response->package_sha256), 1) != OTA_OK) ||
        !is_hex_string(response->package_sha256,
                       OTA_MQTT_SHA256_TEXT_LEN) ||
        (copy_json_string(firmware, "download_url",
                          response->download_url,
                          sizeof(response->download_url), 1) != OTA_OK) ||
        (copy_json_string(firmware, "release_notes",
                          response->release_notes,
                          sizeof(response->release_notes), 0) != OTA_OK) ||
        (response->package_size < BOOT_IMAGE_HEADER_SIZE) ||
        (response->package_size > OTA_SLOT_SIZE) ||
        (strncmp(response->download_url, "http://", 7U) != 0))
    {
        goto exit;
    }
    result = OTA_OK;

exit:
    cJSON_Delete(root);
    return result;
}

int ota_mqtt_build_update_event(
    const ota_mqtt_update_event_t *event,
    char *json,
    size_t json_size)
{
    cJSON *root;

    if ((event == NULL) || (event->message_id[0] == '\0') ||
        (event->device_id[0] == '\0'))
    {
        return OTA_ERROR_ARGUMENT;
    }
    root = cJSON_CreateObject();
    if ((root == NULL) ||
        !add_string(root, "protocol", OTA_MQTT_PROTOCOL_NAME) ||
        !add_string(root, "type", "update_event") ||
        !add_string(root, "message_id", event->message_id) ||
        !add_string(root, "device_id", event->device_id) ||
        !add_string(root, "firmware_message_id",
                    event->firmware_message_id) ||
        !add_string(root, "event", ota_mqtt_event_name(event->event)) ||
        (cJSON_AddNumberToObject(root, "version_code",
                                 event->version_code) == NULL) ||
        (cJSON_AddNumberToObject(root, "progress_percent",
                                 event->progress_percent) == NULL) ||
        (cJSON_AddNumberToObject(root, "received_size",
                                 event->received_size) == NULL) ||
        (cJSON_AddNumberToObject(root, "package_size",
                                 event->package_size) == NULL) ||
        (cJSON_AddNumberToObject(root, "error_code",
                                 event->error_code) == NULL) ||
        !add_string(root, "stage", event->stage) ||
        !add_string(root, "error_message", event->error_message))
    {
        cJSON_Delete(root);
        return OTA_ERROR_NO_MEMORY;
    }
    return print_json(root, json, json_size);
}

int ota_mqtt_build_device_status(
    const ota_mqtt_device_status_t *status,
    char *json,
    size_t json_size)
{
    cJSON *root;

    if ((status == NULL) || (status->device_id[0] == '\0'))
    {
        return OTA_ERROR_ARGUMENT;
    }
    root = cJSON_CreateObject();
    if ((root == NULL) ||
        !add_string(root, "protocol", OTA_MQTT_PROTOCOL_NAME) ||
        !add_string(root, "type", "device_status") ||
        !add_string(root, "device_id", status->device_id) ||
        (cJSON_AddBoolToObject(root, "online", status->online) == NULL) ||
        !add_string(root, "current_version", status->current_version) ||
        (cJSON_AddNumberToObject(root, "current_version_code",
                                 status->current_version_code) == NULL))
    {
        cJSON_Delete(root);
        return OTA_ERROR_NO_MEMORY;
    }
    return print_json(root, json, json_size);
}
