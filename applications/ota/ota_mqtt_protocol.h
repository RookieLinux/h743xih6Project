#ifndef APP_OTA_MQTT_PROTOCOL_H
#define APP_OTA_MQTT_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

/* Transport-independent JSON contract for batch OTA. */
#define OTA_MQTT_PROTOCOL_NAME              "ota-v1"
#define OTA_MQTT_QOS                        1U

#define OTA_MQTT_TOPIC_VERSION_REQUEST      \
    "ota/v1/device/%s/version/request"
#define OTA_MQTT_TOPIC_VERSION_RESPONSE     \
    "ota/v1/device/%s/version/response"
#define OTA_MQTT_TOPIC_UPDATE_EVENT         \
    "ota/v1/device/%s/update/event"
#define OTA_MQTT_TOPIC_STATUS               \
    "ota/v1/device/%s/status"

#define OTA_MQTT_SERVER_VERSION_REQUEST     \
    "ota/v1/device/+/version/request"
#define OTA_MQTT_SERVER_UPDATE_EVENT        \
    "ota/v1/device/+/update/event"

#define OTA_MQTT_DEVICE_ID_MAX_LEN          47U
#define OTA_MQTT_MESSAGE_ID_MAX_LEN         47U
#define OTA_MQTT_VERSION_TEXT_MAX_LEN       23U
#define OTA_MQTT_URL_MAX_LEN                191U
#define OTA_MQTT_SHA256_TEXT_LEN            64U
#define OTA_MQTT_RELEASE_NOTES_MAX_LEN      191U
#define OTA_MQTT_STAGE_MAX_LEN              23U
#define OTA_MQTT_ERROR_TEXT_MAX_LEN         95U
#define OTA_MQTT_BOOT_VERSION_MAX_LEN       23U

typedef enum
{
    OTA_MQTT_EVENT_UPDATE_ACCEPTED = 0,
    OTA_MQTT_EVENT_UPDATE_DECLINED,
    OTA_MQTT_EVENT_DOWNLOAD_STARTED,
    OTA_MQTT_EVENT_DOWNLOAD_PROGRESS,
    OTA_MQTT_EVENT_VERIFYING,
    OTA_MQTT_EVENT_COMMITTED,
    OTA_MQTT_EVENT_REBOOTING,
    OTA_MQTT_EVENT_COMPLETED,
    OTA_MQTT_EVENT_FAILED
} ota_mqtt_event_type_t;

typedef struct
{
    char message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    char device_id[OTA_MQTT_DEVICE_ID_MAX_LEN + 1U];
    uint32_t hardware_id;
    uint32_t current_version_code;
    char current_version[OTA_MQTT_VERSION_TEXT_MAX_LEN + 1U];
    char bootloader_version[OTA_MQTT_BOOT_VERSION_MAX_LEN + 1U];
} ota_mqtt_version_request_t;

typedef struct
{
    char message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    char request_message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    char device_id[OTA_MQTT_DEVICE_ID_MAX_LEN + 1U];
    uint32_t hardware_id;
    uint8_t update_available;
    uint8_t mandatory;
    uint32_t rollout_delay_max_seconds;
    uint32_t version_code;
    uint32_t package_size;
    uint32_t package_crc32;
    char version[OTA_MQTT_VERSION_TEXT_MAX_LEN + 1U];
    char package_sha256[OTA_MQTT_SHA256_TEXT_LEN + 1U];
    char download_url[OTA_MQTT_URL_MAX_LEN + 1U];
    char release_notes[OTA_MQTT_RELEASE_NOTES_MAX_LEN + 1U];
} ota_mqtt_version_response_t;

typedef struct
{
    char message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    char device_id[OTA_MQTT_DEVICE_ID_MAX_LEN + 1U];
    char firmware_message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    ota_mqtt_event_type_t event;
    uint32_t version_code;
    uint32_t received_size;
    uint32_t package_size;
    int32_t error_code;
    uint8_t progress_percent;
    char stage[OTA_MQTT_STAGE_MAX_LEN + 1U];
    char error_message[OTA_MQTT_ERROR_TEXT_MAX_LEN + 1U];
} ota_mqtt_update_event_t;

typedef struct
{
    char device_id[OTA_MQTT_DEVICE_ID_MAX_LEN + 1U];
    uint32_t current_version_code;
    uint8_t online;
    char current_version[OTA_MQTT_VERSION_TEXT_MAX_LEN + 1U];
} ota_mqtt_device_status_t;

/*
 * Builders write compact, NUL-terminated JSON to caller-owned storage.
 * The parser accepts a byte span, so MQTT payloads need not be NUL-terminated.
 * All functions return OTA_OK or an ota_result_t error.
 */
int ota_mqtt_build_version_request(
    const ota_mqtt_version_request_t *request,
    char *json,
    size_t json_size);
int ota_mqtt_parse_version_response(
    const char *json,
    size_t json_length,
    ota_mqtt_version_response_t *response);
int ota_mqtt_build_update_event(
    const ota_mqtt_update_event_t *event,
    char *json,
    size_t json_size);
int ota_mqtt_build_device_status(
    const ota_mqtt_device_status_t *status,
    char *json,
    size_t json_size);
const char *ota_mqtt_event_name(ota_mqtt_event_type_t event);

#endif
