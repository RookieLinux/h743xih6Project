#include "ota_remote.h"

#include <board.h>
#include <rtthread.h>
#include <string.h>
#include <umqtt.h>
#include <umqtt_internal.h>
#include <wlan_mgnt.h>

#include "lv_wifi_weather/inc/lv_wifi_weather.h"
#include "ota.h"
#include "ota_config.h"
#include "ota_http.h"

#define DBG_TAG "app.ota.remote"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define OTA_REMOTE_MQTT_PORT 1883U

typedef enum
{
    OTA_REMOTE_CMD_MQTT_MESSAGE = 0,
    OTA_REMOTE_CMD_CHECK,
    OTA_REMOTE_CMD_ACCEPT,
    OTA_REMOTE_CMD_DECLINE
} ota_remote_command_type_t;

typedef struct
{
    ota_remote_command_type_t type;
    uint16_t length;
    char payload[OTA_MQTT_JSON_MAX_SIZE + 1U];
} ota_remote_command_t;

typedef struct
{
    uint8_t qos;
    uint8_t retained;
    uint16_t length;
    char topic[OTA_MQTT_TOPIC_MAX_SIZE];
    char payload[OTA_MQTT_JSON_MAX_SIZE + 1U];
} ota_mqtt_tx_message_t;

#define OTA_MQTT_TX_SLOT_SIZE \
    (RT_ALIGN(sizeof(ota_mqtt_tx_message_t), RT_ALIGN_SIZE) + \
     sizeof(void *))

static struct rt_messagequeue mqtt_tx_queue_object;
static uint8_t mqtt_tx_queue_pool[
    OTA_MQTT_TX_SLOT_SIZE * OTA_MQTT_TX_QUEUE_DEPTH];
static struct rt_thread mqtt_tx_thread_object;
static rt_uint32_t mqtt_tx_thread_stack[
    OTA_MQTT_TX_THREAD_STACK_SIZE / sizeof(rt_uint32_t)];

typedef struct
{
    char broker_uri[OTA_REMOTE_BROKER_URI_MAX_LEN + 1U];
    char pending_broker_uri[OTA_REMOTE_BROKER_URI_MAX_LEN + 1U];
    char username[OTA_REMOTE_CREDENTIAL_MAX_LEN + 1U];
    char password[OTA_REMOTE_CREDENTIAL_MAX_LEN + 1U];
    char device_id[OTA_MQTT_DEVICE_ID_MAX_LEN + 1U];
    char response_topic[OTA_MQTT_TOPIC_MAX_SIZE];
    char request_topic[OTA_MQTT_TOPIC_MAX_SIZE];
    char event_topic[OTA_MQTT_TOPIC_MAX_SIZE];
    char status_topic[OTA_MQTT_TOPIC_MAX_SIZE];
    char last_request_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    char will_json[OTA_MQTT_JSON_MAX_SIZE + 1U];
    ota_remote_storage_ops_t storage;
    uint32_t message_counter;
    uint8_t auto_reboot;
    uint8_t enable_rollout_delay;
    uint8_t update_valid;
    uint8_t update_in_progress;
    ota_http_stage_t download_stage;
    ota_mqtt_version_response_t update;
    ota_remote_command_t rx_command;
    lvww_firmware_info_t ui_info;
    lvww_ctx_t *ui;
    umqtt_client_t mqtt;
    rt_mq_t queue;
    rt_mq_t tx_queue;
    rt_mutex_t lock;
    rt_mutex_t mqtt_lock;
    rt_thread_t thread;
    rt_thread_t tx_thread;
    uint8_t mqtt_ready;
    uint8_t mqtt_connect_stage;
    uint8_t broker_update_pending;
    rt_tick_t last_ui_progress_tick;
    rt_tick_t last_mqtt_progress_tick;
    int mqtt_last_result;
} ota_remote_context_t;

static ota_remote_context_t remote;

static void copy_text(char *destination,
                      size_t destination_size,
                      const char *source)
{
    if ((destination == RT_NULL) || (destination_size == 0U))
    {
        return;
    }
    destination[0] = '\0';
    if (source != RT_NULL)
    {
        rt_strncpy(destination, source, destination_size - 1U);
        destination[destination_size - 1U] = '\0';
    }
}

static int ipv4_address_valid(const char *address)
{
    unsigned value = 0U;
    unsigned digits = 0U;
    unsigned octets = 0U;
    const char *cursor;

    if ((address == RT_NULL) || !address[0])
    {
        return 0;
    }
    for (cursor = address;; ++cursor)
    {
        char ch = *cursor;
        if ((ch >= '0') && (ch <= '9'))
        {
            value = value * 10U + (unsigned)(ch - '0');
            if ((++digits > 3U) || (value > 255U))
            {
                return 0;
            }
            continue;
        }
        if ((ch != '.') && (ch != '\0'))
        {
            return 0;
        }
        if (digits == 0U)
        {
            return 0;
        }
        ++octets;
        if (ch == '\0')
        {
            return octets == 4U;
        }
        if (octets >= 4U)
        {
            return 0;
        }
        value = 0U;
        digits = 0U;
    }
}

static int broker_uri_server_ip(const char *uri,
                                char *address,
                                size_t address_size)
{
    static const char prefix[] = "tcp://";
    const char *start;
    const char *end;
    size_t length;

    if ((uri == RT_NULL) || (address == RT_NULL) ||
        (address_size == 0U) ||
        (strncmp(uri, prefix, sizeof(prefix) - 1U) != 0))
    {
        return OTA_ERROR_ARGUMENT;
    }
    start = uri + sizeof(prefix) - 1U;
    end = strchr(start, ':');
    if (end == RT_NULL)
    {
        end = start + strlen(start);
    }
    length = (size_t)(end - start);
    if ((length == 0U) || (length >= address_size))
    {
        return OTA_ERROR_SIZE;
    }
    memcpy(address, start, length);
    address[length] = '\0';
    return ipv4_address_valid(address) ? OTA_OK : OTA_ERROR_ARGUMENT;
}

static int apply_pending_broker_uri(void)
{
    int changed = 0;

    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    if (remote.broker_update_pending)
    {
        copy_text(remote.broker_uri, sizeof(remote.broker_uri),
                  remote.pending_broker_uri);
        remote.pending_broker_uri[0] = '\0';
        remote.broker_update_pending = 0U;
        changed = 1;
    }
    rt_mutex_release(remote.lock);
    if (changed)
    {
        LOG_I("MQTT broker updated to %s", remote.broker_uri);
    }
    return changed;
}

static void make_message_id(char *output, size_t output_size)
{
    ++remote.message_counter;
    rt_snprintf(output, output_size, "%s-%u",
                remote.device_id, (unsigned)remote.message_counter);
}

static void ui_publish(void)
{
    lvww_ctx_t *ui;
    lvww_firmware_info_t info;

    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    ui = remote.ui;
    info = remote.ui_info;
    rt_mutex_release(remote.lock);
    if (ui != RT_NULL)
    {
        lvww_set_firmware_info(ui, &info);
    }
}

static void ui_set_state(lvww_firmware_state_t state,
                         uint8_t progress)
{
    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    remote.ui_info.state = state;
    remote.ui_info.progress_percent = progress;
    rt_mutex_release(remote.lock);
    ui_publish();
}

static int enqueue_simple(ota_remote_command_type_t type)
{
    ota_remote_command_t command;

    if (remote.queue == RT_NULL)
    {
        return OTA_ERROR_STATE;
    }
    memset(&command, 0, sizeof(command));
    command.type = type;
    return rt_mq_send(remote.queue, &command, sizeof(command)) == RT_EOK
               ? OTA_OK
               : OTA_ERROR_STATE;
}

static void mqtt_receive_callback(void *client, void *message_data)
{
    struct umqtt_pkgs_publish *message =
        (struct umqtt_pkgs_publish *)message_data;
    ota_remote_command_t *command = &remote.rx_command;

    (void)client;
    if ((message == RT_NULL) || (message->payload == RT_NULL) ||
        (message->payload_len == 0U) ||
        (message->payload_len > OTA_MQTT_JSON_MAX_SIZE) ||
        (remote.queue == RT_NULL))
    {
        return;
    }
    command->type = OTA_REMOTE_CMD_MQTT_MESSAGE;
    command->length = (uint16_t)message->payload_len;
    memcpy(command->payload, message->payload, message->payload_len);
    command->payload[message->payload_len] = '\0';
    if (rt_mq_send(remote.queue, command, sizeof(*command)) != RT_EOK)
    {
        LOG_W("dropping MQTT response: controller queue is full");
    }
}

static int mqtt_is_linked(void)
{
    int state;

    if (remote.mqtt_lock == RT_NULL)
    {
        return 0;
    }
    rt_mutex_take(remote.mqtt_lock, RT_WAITING_FOREVER);
    state = (remote.mqtt_ready && (remote.mqtt != RT_NULL))
                ? umqtt_control(remote.mqtt,
                                UMQTT_CMD_GET_CLIENT_STA, RT_NULL)
                : UMQTT_CS_IDLE;
    rt_mutex_release(remote.mqtt_lock);
    return state == UMQTT_CS_LINKED;
}

static int mqtt_enqueue(const char *topic,
                        const char *json,
                        enum umqtt_qos qos,
                        int retained)
{
    ota_mqtt_tx_message_t message;
    size_t length;

    if ((remote.tx_queue == RT_NULL) || (topic == RT_NULL) ||
        (json == RT_NULL))
    {
        return OTA_ERROR_STATE;
    }
    length = strlen(json);
    if ((length == 0U) || (length > OTA_MQTT_JSON_MAX_SIZE) ||
        (strlen(topic) >= sizeof(message.topic)))
    {
        return OTA_ERROR_SIZE;
    }
    memset(&message, 0, sizeof(message));
    message.qos = (uint8_t)qos;
    message.retained = retained ? 1U : 0U;
    message.length = (uint16_t)length;
    copy_text(message.topic, sizeof(message.topic), topic);
    memcpy(message.payload, json, length + 1U);
    if (rt_mq_send(remote.tx_queue, &message, sizeof(message)) != RT_EOK)
    {
        if (qos != UMQTT_QOS0)
        {
            LOG_W("dropping MQTT publish: transmit queue is full");
        }
        return OTA_ERROR_STATE;
    }
    return OTA_OK;
}

static int mqtt_publish_json(const char *topic,
                             const char *json,
                             int retained)
{
    return mqtt_enqueue(topic, json, UMQTT_QOS1, retained);
}

static int mqtt_publish_progress(const char *topic, const char *json)
{
    return mqtt_enqueue(topic, json, UMQTT_QOS0, 0);
}

static void mqtt_tx_entry(void *parameter)
{
    ota_mqtt_tx_message_t message;
    (void)parameter;

    for (;;)
    {
        int result = -1;

        if (rt_mq_recv(remote.tx_queue, &message, sizeof(message),
                       RT_WAITING_FOREVER) != RT_EOK)
        {
            continue;
        }
        rt_mutex_take(remote.mqtt_lock, RT_WAITING_FOREVER);
        if (remote.mqtt_ready && (remote.mqtt != RT_NULL) &&
            (umqtt_control(remote.mqtt, UMQTT_CMD_GET_CLIENT_STA,
                           RT_NULL) == UMQTT_CS_LINKED))
        {
            if (message.qos == (uint8_t)UMQTT_QOS0)
            {
                result = umqtt_publish_async(
                    remote.mqtt, UMQTT_QOS0, message.topic,
                    message.payload, message.length);
            }
            else
            {
                result = umqtt_publish_ex(
                    remote.mqtt, UMQTT_QOS1, message.topic,
                    message.payload, message.length,
                    OTA_MQTT_PUBLISH_TIMEOUT_MS,
                    message.retained);
            }
        }
        rt_mutex_release(remote.mqtt_lock);
        if (result < 0)
        {
            LOG_W("MQTT queued publish failed: %s", message.topic);
        }
    }
}

static int publish_status(uint8_t online)
{
    ota_mqtt_device_status_t status;
    char json[OTA_MQTT_JSON_MAX_SIZE + 1U];
    int result;

    memset(&status, 0, sizeof(status));
    copy_text(status.device_id, sizeof(status.device_id), remote.device_id);
    copy_text(status.current_version, sizeof(status.current_version),
              OTA_CURRENT_FIRMWARE_VERSION_TEXT);
    status.current_version_code = OTA_CURRENT_FIRMWARE_VERSION_CODE;
    status.online = online;
    result = ota_mqtt_build_device_status(&status, json, sizeof(json));
    return result == OTA_OK
               ? mqtt_publish_json(remote.status_topic, json, 1)
               : result;
}

static int publish_version_request(void)
{
    ota_mqtt_version_request_t request;
    char json[OTA_MQTT_JSON_MAX_SIZE + 1U];
    int result;

    memset(&request, 0, sizeof(request));
    make_message_id(request.message_id, sizeof(request.message_id));
    copy_text(remote.last_request_id, sizeof(remote.last_request_id),
              request.message_id);
    copy_text(request.device_id, sizeof(request.device_id), remote.device_id);
    copy_text(request.current_version, sizeof(request.current_version),
              OTA_CURRENT_FIRMWARE_VERSION_TEXT);
    copy_text(request.bootloader_version,
              sizeof(request.bootloader_version),
              OTA_BOOTLOADER_VERSION_TEXT);
    request.hardware_id = OTA_HARDWARE_ID;
    request.current_version_code = OTA_CURRENT_FIRMWARE_VERSION_CODE;

    result = ota_mqtt_build_version_request(&request, json, sizeof(json));
    if (result == OTA_OK)
    {
        result = mqtt_publish_json(remote.request_topic, json, 0);
    }
    return result;
}

static int publish_update_event_internal(
    ota_mqtt_event_type_t event_type,
    const char *stage,
    uint32_t received,
    uint8_t percent,
    int error,
    int progress_qos0)
{
    ota_mqtt_update_event_t event;
    char json[OTA_MQTT_JSON_MAX_SIZE + 1U];
    int result;

    memset(&event, 0, sizeof(event));
    make_message_id(event.message_id, sizeof(event.message_id));
    copy_text(event.device_id, sizeof(event.device_id), remote.device_id);
    copy_text(event.firmware_message_id,
              sizeof(event.firmware_message_id),
              remote.update.message_id);
    copy_text(event.stage, sizeof(event.stage), stage);
    if (error != OTA_OK)
    {
        copy_text(event.error_message, sizeof(event.error_message),
                  ota_result_string(error));
    }
    event.event = event_type;
    event.version_code = remote.update.version_code;
    event.received_size = received;
    event.package_size = remote.update.package_size;
    event.progress_percent = percent;
    event.error_code = error;
    result = ota_mqtt_build_update_event(&event, json, sizeof(json));
    if (result != OTA_OK)
    {
        return result;
    }
    return progress_qos0
               ? mqtt_publish_progress(remote.event_topic, json)
               : mqtt_publish_json(remote.event_topic, json, 0);
}

static int publish_update_event(ota_mqtt_event_type_t event_type,
                                const char *stage,
                                uint32_t received,
                                uint8_t percent,
                                int error)
{
    return publish_update_event_internal(event_type, stage, received,
                                         percent, error, 0);
}

static void publish_completed_if_pending(void)
{
    ota_remote_pending_update_t pending;

    if (remote.storage.load == RT_NULL)
    {
        return;
    }
    memset(&pending, 0, sizeof(pending));
    if ((remote.storage.load(remote.storage.user_ctx, &pending) == OTA_OK) &&
        (pending.target_version_code == OTA_CURRENT_FIRMWARE_VERSION_CODE) &&
        (pending.firmware_message_id[0] != '\0'))
    {
        memset(&remote.update, 0, sizeof(remote.update));
        copy_text(remote.update.message_id, sizeof(remote.update.message_id),
                  pending.firmware_message_id);
        remote.update.version_code = pending.target_version_code;
        if ((publish_update_event(OTA_MQTT_EVENT_COMPLETED,
                                  "boot", 0U, 100U, OTA_OK) == OTA_OK) &&
            (remote.storage.clear != RT_NULL))
        {
            remote.storage.clear(remote.storage.user_ctx);
        }
    }
}

static int http_progress(void *user_ctx,
                         ota_http_stage_t stage,
                         uint32_t received,
                         uint32_t total,
                         uint8_t percent)
{
    rt_tick_t now;

    (void)user_ctx;
    (void)total;
    remote.download_stage = stage;

    if (!rt_wlan_is_ready())
    {
        return OTA_ERROR_NETWORK;
    }
    if (stage == OTA_HTTP_STAGE_DOWNLOAD)
    {
        now = rt_tick_get();
        if (percent == 0U)
        {
            remote.last_ui_progress_tick = now;
            remote.last_mqtt_progress_tick = now;
            ui_set_state(LVWW_FIRMWARE_DOWNLOADING, percent);
            publish_update_event(OTA_MQTT_EVENT_DOWNLOAD_STARTED,
                                 "download", received, percent, OTA_OK);
        }
        else
        {
            if ((percent == 100U) ||
                ((rt_tick_t)(now - remote.last_ui_progress_tick) >=
                 rt_tick_from_millisecond(OTA_UI_PROGRESS_INTERVAL_MS)))
            {
                remote.last_ui_progress_tick = now;
                ui_set_state(LVWW_FIRMWARE_DOWNLOADING, percent);
            }

            /* The following QoS 1 verify/commit events carry the terminal
             * 100% state. Keep progress best-effort and sparse so it cannot
             * fill the transmit queue ahead of those reliable events. */
            if ((percent < 100U) &&
                ((rt_tick_t)(now - remote.last_mqtt_progress_tick) >=
                 rt_tick_from_millisecond(
                     OTA_MQTT_PROGRESS_INTERVAL_MS)))
            {
                remote.last_mqtt_progress_tick = now;
                publish_update_event_internal(
                    OTA_MQTT_EVENT_DOWNLOAD_PROGRESS,
                    "download", received, percent, OTA_OK, 1);
            }
        }
    }
    else if (stage == OTA_HTTP_STAGE_VERIFY)
    {
        ui_set_state(LVWW_FIRMWARE_VERIFYING, 100U);
        publish_update_event(OTA_MQTT_EVENT_VERIFYING,
                             "verify", received, 100U, OTA_OK);
    }
    else
    {
        ui_set_state(LVWW_FIRMWARE_READY, 100U);
        publish_update_event(OTA_MQTT_EVENT_COMMITTED,
                             "commit", received, 100U, OTA_OK);
    }
    return OTA_OK;
}

static int save_pending_update(void)
{
    ota_remote_pending_update_t pending;

    if (remote.storage.save == RT_NULL)
    {
        return OTA_OK;
    }
    memset(&pending, 0, sizeof(pending));
    copy_text(pending.firmware_message_id,
              sizeof(pending.firmware_message_id),
              remote.update.message_id);
    pending.target_version_code = remote.update.version_code;
    return remote.storage.save(remote.storage.user_ctx, &pending);
}

static void run_update(void)
{
    ota_http_request_t request;
    const char *failure_stage = "download";
    int result;

    if (!remote.update_valid)
    {
        return;
    }
    remote.download_stage = OTA_HTTP_STAGE_DOWNLOAD;
    remote.last_ui_progress_tick = 0;
    remote.last_mqtt_progress_tick = 0;
    publish_update_event(OTA_MQTT_EVENT_UPDATE_ACCEPTED,
                         "decision", 0U, 0U, OTA_OK);
    if (remote.enable_rollout_delay &&
        (remote.update.rollout_delay_max_seconds != 0U))
    {
        uint32_t delay = (HAL_GetUIDw0() ^ rt_tick_get()) %
                         (remote.update.rollout_delay_max_seconds + 1U);
        while ((delay-- != 0U) && rt_wlan_is_ready())
        {
            rt_thread_mdelay(1000);
        }
    }
    if (!rt_wlan_is_ready())
    {
        result = OTA_ERROR_NETWORK;
        goto failed;
    }

    request.url = remote.update.download_url;
    request.package_size = remote.update.package_size;
    request.package_crc32 = remote.update.package_crc32;
    request.package_sha256 = remote.update.package_sha256;
    result = ota_http_download(&request, http_progress, RT_NULL);
    if (result != OTA_OK)
    {
        goto failed;
    }

    result = save_pending_update();
    if (result != OTA_OK)
    {
        failure_stage = "persistence";
        goto failed;
    }
    remote.update_valid = 0U;
    if (remote.auto_reboot)
    {
        publish_update_event(OTA_MQTT_EVENT_REBOOTING,
                             "reboot", remote.update.package_size,
                             100U, OTA_OK);
        ota_reboot_to_install(1000U);
    }
    remote.update_in_progress = 0U;
    return;

failed:
    if ((strcmp(failure_stage, "download") == 0) &&
        (remote.download_stage == OTA_HTTP_STAGE_VERIFY))
    {
        failure_stage = "verify";
    }
    else if ((strcmp(failure_stage, "download") == 0) &&
             (remote.download_stage == OTA_HTTP_STAGE_COMMIT))
    {
        failure_stage = "commit";
    }
    remote.update_in_progress = 0U;
    ui_set_state(LVWW_FIRMWARE_ERROR, 0U);
    publish_update_event(OTA_MQTT_EVENT_FAILED,
                         failure_stage, 0U, 0U, result);
}

static void handle_version_response(const ota_remote_command_t *command)
{
    ota_mqtt_version_response_t response;
    int result = ota_mqtt_parse_version_response(
        command->payload, command->length, &response);

    if (result != OTA_OK)
    {
        LOG_W("invalid version response (%d)", result);
        return;
    }
    if ((strcmp(response.request_message_id,
                remote.last_request_id) != 0) ||
        (strcmp(response.device_id, remote.device_id) != 0) ||
        (response.hardware_id != OTA_HARDWARE_ID))
    {
        LOG_W("version response does not match this request/device");
        return;
    }

    if (!response.update_available ||
        (response.version_code <= OTA_CURRENT_FIRMWARE_VERSION_CODE))
    {
        remote.update_valid = 0U;
        rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
        remote.ui_info.state = LVWW_FIRMWARE_UP_TO_DATE;
        remote.ui_info.available_version_code = 0U;
        remote.ui_info.available_version[0] = '\0';
        remote.ui_info.release_notes[0] = '\0';
        rt_mutex_release(remote.lock);
        ui_publish();
        return;
    }
    if (remote.update_valid &&
        (strcmp(remote.update.message_id, response.message_id) == 0))
    {
        return;
    }

    remote.update = response;
    remote.update_valid = 1U;
    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    remote.ui_info.state = LVWW_FIRMWARE_AVAILABLE;
    remote.ui_info.available_version_code = response.version_code;
    remote.ui_info.package_size = response.package_size;
    remote.ui_info.mandatory = response.mandatory;
    copy_text(remote.ui_info.available_version,
              sizeof(remote.ui_info.available_version),
              response.version);
    copy_text(remote.ui_info.release_notes,
              sizeof(remote.ui_info.release_notes),
              response.release_notes);
    rt_mutex_release(remote.lock);
    ui_publish();
}

static void disconnect_mqtt(void)
{
    if (remote.mqtt_lock != RT_NULL)
    {
        rt_mutex_take(remote.mqtt_lock, RT_WAITING_FOREVER);
    }
    remote.mqtt_ready = 0U;
    if (remote.mqtt != RT_NULL)
    {
        umqtt_stop(remote.mqtt);
        umqtt_delete(remote.mqtt);
        remote.mqtt = RT_NULL;
    }
    if (remote.mqtt_lock != RT_NULL)
    {
        rt_mutex_release(remote.mqtt_lock);
    }
}

static int connect_mqtt(void)
{
    struct umqtt_info info;
    int mqtt_result;

    memset(&info, 0, sizeof(info));
    info.uri = remote.broker_uri;
    info.client_id = remote.device_id;
    info.user_name = remote.username[0] ? remote.username : RT_NULL;
    info.password = remote.password[0] ? remote.password : RT_NULL;
    info.lwt_topic = remote.status_topic;
    info.lwt_message = remote.will_json;
    info.lwt_qos = UMQTT_QOS1;
    info.lwt_retain = 1U;
    info.send_size = 2048U;
    info.recv_size = 2048U;
    info.reconnect_max_num = 1U;
    info.reconnect_interval = 2U;
    info.keepalive_interval = OTA_MQTT_KEEPALIVE_SECONDS;
    info.thread_stack_size = 3072U;
    info.thread_priority = OTA_MQTT_THREAD_PRIORITY - 1U;

    remote.mqtt_connect_stage = 1U;
    remote.mqtt_last_result = OTA_OK;
    rt_mutex_take(remote.mqtt_lock, RT_WAITING_FOREVER);
    remote.mqtt = umqtt_create(&info);
    if (remote.mqtt == RT_NULL)
    {
        remote.mqtt_last_result = OTA_ERROR_NO_MEMORY;
        rt_mutex_release(remote.mqtt_lock);
        return OTA_ERROR_NO_MEMORY;
    }
    remote.mqtt_connect_stage = 2U;
    mqtt_result = umqtt_start(remote.mqtt);
    if (mqtt_result >= 0)
    {
        remote.mqtt_connect_stage = 3U;
        mqtt_result = umqtt_subscribe(
            remote.mqtt, remote.response_topic,
            UMQTT_QOS1, mqtt_receive_callback);
    }
    if (mqtt_result < 0)
    {
        remote.mqtt_last_result = mqtt_result;
        umqtt_stop(remote.mqtt);
        umqtt_delete(remote.mqtt);
        remote.mqtt = RT_NULL;
        rt_mutex_release(remote.mqtt_lock);
        return OTA_ERROR_NETWORK;
    }
    remote.mqtt_ready = 1U;
    remote.mqtt_connect_stage = 4U;
    remote.mqtt_last_result = mqtt_result;
    rt_mutex_release(remote.mqtt_lock);
    publish_status(1U);
    publish_completed_if_pending();
    return publish_version_request();
}

static void ota_remote_entry(void *parameter)
{
    ota_remote_command_t command;
    uint8_t reconnect_requested;
    (void)parameter;

    for (;;)
    {
        while (!rt_wlan_is_ready())
        {
            apply_pending_broker_uri();
            rt_thread_mdelay(1000);
        }
        apply_pending_broker_uri();
        if (connect_mqtt() != OTA_OK)
        {
            LOG_W("MQTT connect failed; retrying");
            disconnect_mqtt();
            rt_thread_mdelay(OTA_MQTT_RETRY_DELAY_MS);
            continue;
        }
        LOG_I("MQTT OTA online as %s", remote.device_id);
        reconnect_requested = 0U;

        while (rt_wlan_is_ready() && mqtt_is_linked())
        {
            if (apply_pending_broker_uri())
            {
                reconnect_requested = 1U;
                break;
            }
            if (rt_mq_recv(remote.queue, &command, sizeof(command),
                           rt_tick_from_millisecond(1000)) != RT_EOK)
            {
                continue;
            }
            switch (command.type)
            {
            case OTA_REMOTE_CMD_MQTT_MESSAGE:
                handle_version_response(&command);
                break;
            case OTA_REMOTE_CMD_CHECK:
                ui_set_state(LVWW_FIRMWARE_CHECKING, 0U);
                publish_version_request();
                break;
            case OTA_REMOTE_CMD_ACCEPT:
                run_update();
                break;
            case OTA_REMOTE_CMD_DECLINE:
                if (remote.update_valid)
                {
                    publish_update_event(OTA_MQTT_EVENT_UPDATE_DECLINED,
                                         "decision", 0U, 0U, OTA_OK);
                }
                break;
            default:
                break;
            }
        }
        disconnect_mqtt();
        if (reconnect_requested)
        {
            LOG_I("MQTT OTA reconnecting with the new server");
        }
        else
        {
            LOG_W("MQTT OTA offline");
            rt_thread_mdelay(OTA_MQTT_RETRY_DELAY_MS);
        }
    }
}

static int ui_update_callback(void *user_ctx,
                              const lvww_firmware_info_t *firmware)
{
    (void)user_ctx;
    (void)firmware;
    return ota_remote_accept_update() == OTA_OK ? RT_EOK : -RT_ERROR;
}

static int ui_server_address_callback(void *user_ctx,
                                      const char *ipv4_address)
{
    (void)user_ctx;
    return ota_remote_set_server_ip(ipv4_address) == OTA_OK
               ? RT_EOK
               : -RT_EINVAL;
}

void ota_remote_default_config(ota_remote_config_t *config)
{
    if (config == RT_NULL)
    {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->broker_uri = OTA_MQTT_BROKER_URI;
    config->username = OTA_MQTT_USERNAME;
    config->password = OTA_MQTT_PASSWORD;
    config->auto_reboot = 1U;
    config->enable_rollout_delay = 1U;
}

int ota_remote_start(const ota_remote_config_t *config)
{
    ota_remote_config_t defaults;
    ota_mqtt_device_status_t will_status;
    int result;

    if (remote.thread != RT_NULL)
    {
        return OTA_OK;
    }
    if (config == RT_NULL)
    {
        ota_remote_default_config(&defaults);
        config = &defaults;
    }
    if ((config->broker_uri == RT_NULL) ||
        (strlen(config->broker_uri) > OTA_REMOTE_BROKER_URI_MAX_LEN) ||
        ((config->username != RT_NULL) &&
         (strlen(config->username) > OTA_REMOTE_CREDENTIAL_MAX_LEN)) ||
        ((config->password != RT_NULL) &&
         (strlen(config->password) > OTA_REMOTE_CREDENTIAL_MAX_LEN)))
    {
        return OTA_ERROR_ARGUMENT;
    }
    result = ota_service_init();
    if (result != OTA_OK)
    {
        return result;
    }

    memset(&remote, 0, sizeof(remote));
    copy_text(remote.broker_uri, sizeof(remote.broker_uri),
              config->broker_uri);
    copy_text(remote.username, sizeof(remote.username), config->username);
    copy_text(remote.password, sizeof(remote.password), config->password);
    remote.auto_reboot = config->auto_reboot;
    remote.enable_rollout_delay = config->enable_rollout_delay;
    remote.storage = config->storage;
    result = ota_remote_get_device_id(remote.device_id,
                                      sizeof(remote.device_id));
    if (result != OTA_OK)
    {
        return result;
    }
    rt_snprintf(remote.response_topic, sizeof(remote.response_topic),
                OTA_MQTT_TOPIC_VERSION_RESPONSE, remote.device_id);
    rt_snprintf(remote.request_topic, sizeof(remote.request_topic),
                OTA_MQTT_TOPIC_VERSION_REQUEST, remote.device_id);
    rt_snprintf(remote.event_topic, sizeof(remote.event_topic),
                OTA_MQTT_TOPIC_UPDATE_EVENT, remote.device_id);
    rt_snprintf(remote.status_topic, sizeof(remote.status_topic),
                OTA_MQTT_TOPIC_STATUS, remote.device_id);

    memset(&will_status, 0, sizeof(will_status));
    copy_text(will_status.device_id, sizeof(will_status.device_id),
              remote.device_id);
    copy_text(will_status.current_version,
              sizeof(will_status.current_version),
              OTA_CURRENT_FIRMWARE_VERSION_TEXT);
    will_status.current_version_code = OTA_CURRENT_FIRMWARE_VERSION_CODE;
    will_status.online = 0U;
    result = ota_mqtt_build_device_status(
        &will_status, remote.will_json, sizeof(remote.will_json));
    if (result != OTA_OK)
    {
        return result;
    }

    lvww_firmware_info_init(&remote.ui_info);
    remote.ui_info.state = LVWW_FIRMWARE_CHECKING;
    remote.ui_info.current_version_code =
        OTA_CURRENT_FIRMWARE_VERSION_CODE;
    copy_text(remote.ui_info.current_version,
              sizeof(remote.ui_info.current_version),
              OTA_CURRENT_FIRMWARE_VERSION_TEXT);
    remote.lock = rt_mutex_create("ota_remote", RT_IPC_FLAG_FIFO);
    remote.mqtt_lock = rt_mutex_create("ota_mqtt", RT_IPC_FLAG_FIFO);
    remote.queue = rt_mq_create("ota_cmd", sizeof(ota_remote_command_t),
                                OTA_MQTT_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    result = rt_mq_init(
        &mqtt_tx_queue_object, "ota_tx",
        mqtt_tx_queue_pool, sizeof(ota_mqtt_tx_message_t),
        sizeof(mqtt_tx_queue_pool), RT_IPC_FLAG_FIFO);
    remote.tx_queue = result == RT_EOK ? &mqtt_tx_queue_object : RT_NULL;
    if ((remote.lock == RT_NULL) || (remote.mqtt_lock == RT_NULL) ||
        (remote.queue == RT_NULL) || (remote.tx_queue == RT_NULL))
    {
        return OTA_ERROR_NO_MEMORY;
    }
    result = rt_thread_init(
        &mqtt_tx_thread_object, "ota_tx",
        mqtt_tx_entry, RT_NULL,
        mqtt_tx_thread_stack, sizeof(mqtt_tx_thread_stack),
        OTA_MQTT_TX_THREAD_PRIORITY, 10U);
    remote.tx_thread = result == RT_EOK
                           ? &mqtt_tx_thread_object
                           : RT_NULL;
    remote.thread = rt_thread_create(
        "ota_remote", ota_remote_entry, RT_NULL,
        OTA_MQTT_THREAD_STACK_SIZE,
        OTA_MQTT_THREAD_PRIORITY, 10U);
    if ((remote.tx_thread == RT_NULL) || (remote.thread == RT_NULL))
    {
        return OTA_ERROR_NO_MEMORY;
    }
    rt_thread_startup(remote.tx_thread);
    rt_thread_startup(remote.thread);
    return OTA_OK;
}

void ota_remote_bind_ui(lvww_ctx_t *ui)
{
    char server_ip[LVWW_SERVER_IPV4_MAX_LEN + 1U];

    if (remote.lock == RT_NULL)
    {
        return;
    }
    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    remote.ui = ui;
    rt_mutex_release(remote.lock);
    if (ui != RT_NULL)
    {
        lvww_set_firmware_update_callback(
            ui, ui_update_callback, RT_NULL);
        lvww_set_server_address_callback(
            ui, ui_server_address_callback, RT_NULL);
        if (broker_uri_server_ip(remote.broker_uri,
                                 server_ip, sizeof(server_ip)) == OTA_OK)
        {
            lvww_set_server_address(ui, server_ip);
        }
        ui_publish();
    }
}

int ota_remote_set_server_ip(const char *ipv4_address)
{
    char broker_uri[OTA_REMOTE_BROKER_URI_MAX_LEN + 1U];
    int length;

    if (!ipv4_address_valid(ipv4_address) || (remote.lock == RT_NULL))
    {
        return OTA_ERROR_ARGUMENT;
    }
    length = rt_snprintf(broker_uri, sizeof(broker_uri),
                         "tcp://%s:%u", ipv4_address,
                         (unsigned)OTA_REMOTE_MQTT_PORT);
    if ((length <= 0) || ((size_t)length >= sizeof(broker_uri)))
    {
        return OTA_ERROR_SIZE;
    }

    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    copy_text(remote.pending_broker_uri,
              sizeof(remote.pending_broker_uri), broker_uri);
    remote.broker_update_pending = 1U;
    rt_mutex_release(remote.lock);
    return OTA_OK;
}

int ota_remote_request_version(void)
{
    return enqueue_simple(OTA_REMOTE_CMD_CHECK);
}

int ota_remote_accept_update(void)
{
    int result;

    if (remote.lock == RT_NULL)
    {
        return OTA_ERROR_STATE;
    }
    rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
    if (!remote.update_valid || remote.update_in_progress)
    {
        rt_mutex_release(remote.lock);
        return OTA_ERROR_STATE;
    }
    remote.update_in_progress = 1U;
    rt_mutex_release(remote.lock);
    result = enqueue_simple(OTA_REMOTE_CMD_ACCEPT);
    if (result != OTA_OK)
    {
        rt_mutex_take(remote.lock, RT_WAITING_FOREVER);
        remote.update_in_progress = 0U;
        rt_mutex_release(remote.lock);
    }
    return result;
}

int ota_remote_decline_update(void)
{
    return enqueue_simple(OTA_REMOTE_CMD_DECLINE);
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
int ota_remote_get_device_id(char *device_id, size_t device_id_size)
{
    int length;

    if ((device_id == RT_NULL) || (device_id_size == 0U))
    {
        return OTA_ERROR_ARGUMENT;
    }
    length = rt_snprintf(device_id, device_id_size,
                         "H743-%08X%08X%08X",
                         (unsigned)HAL_GetUIDw0(),
                         (unsigned)HAL_GetUIDw1(),
                         (unsigned)HAL_GetUIDw2());
    return ((length > 0) && ((size_t)length < device_id_size))
               ? OTA_OK
               : OTA_ERROR_SIZE;
}
