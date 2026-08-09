#ifndef APP_OTA_REMOTE_H
#define APP_OTA_REMOTE_H

#include <stddef.h>
#include <stdint.h>

#include "ota_mqtt_protocol.h"

typedef struct lvww_ctx lvww_ctx_t;

#define OTA_REMOTE_BROKER_URI_MAX_LEN  127U
#define OTA_REMOTE_CREDENTIAL_MAX_LEN  63U

typedef struct
{
    char firmware_message_id[OTA_MQTT_MESSAGE_ID_MAX_LEN + 1U];
    uint32_t target_version_code;
} ota_remote_pending_update_t;

/*
 * Optional persistence hooks. Implement these with EasyFlash, a reserved
 * partition, or another durable KV store to correlate the "completed" event
 * after reboot. A NULL hook leaves this feature disabled.
 */
typedef struct
{
    int (*load)(void *user_ctx, ota_remote_pending_update_t *pending);
    int (*save)(void *user_ctx,
                const ota_remote_pending_update_t *pending);
    int (*clear)(void *user_ctx);
    void *user_ctx;
} ota_remote_storage_ops_t;

typedef struct
{
    const char *broker_uri;
    const char *username;
    const char *password;
    uint8_t auto_reboot;
    uint8_t enable_rollout_delay;
    ota_remote_storage_ops_t storage;
} ota_remote_config_t;

void ota_remote_default_config(ota_remote_config_t *config);
int ota_remote_start(const ota_remote_config_t *config);
void ota_remote_bind_ui(lvww_ctx_t *ui);
int ota_remote_set_server_ip(const char *ipv4_address);
int ota_remote_request_version(void);
int ota_remote_accept_update(void);
int ota_remote_decline_update(void);

/*
 * Weak platform hook. The default STM32 implementation uses the 96-bit UID.
 * Override it when device IDs are provisioned by manufacturing.
 */
int ota_remote_get_device_id(char *device_id, size_t device_id_size);

#endif
