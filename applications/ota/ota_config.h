#ifndef APP_OTA_CONFIG_H

/*
 * Keep deployment credentials out of source control. A developer may copy
 * this file to ota_config_local.h, set the broker values there, and the local
 * file will take precedence without being committed.
 */
#if defined(__has_include)
#  if __has_include("ota_config_local.h")
#    include "ota_config_local.h"
#  endif
#endif

#ifndef APP_OTA_CONFIG_H
#define APP_OTA_CONFIG_H

#define OTA_DOWNLOAD_PARTITION_NAME  "download"
#define OTA_UPGRADE_PARTITION_NAME   "upgrade"

#define OTA_FLASH_SECTOR_SIZE         4096UL
#define OTA_SLOT_SIZE                 (1024UL * 1024UL)
#define OTA_IMAGE_PAYLOAD_OFFSET      OTA_FLASH_SECTOR_SIZE
#define OTA_IMAGE_MAX_SIZE            (OTA_SLOT_SIZE - OTA_IMAGE_PAYLOAD_OFFSET)

#define OTA_APP_BASE                  0x08100000UL
#define OTA_HARDWARE_ID               0x48373433UL

/*
 * Keep these values identical to the --version passed to Bootloader
 * tools/mkimage.py for this APP build.
 */
#define OTA_CURRENT_FIRMWARE_VERSION_CODE  0x00010001UL
#define OTA_CURRENT_FIRMWARE_VERSION_TEXT  "V1.0.1"

#define OTA_COPY_BUFFER_SIZE          1024U
#define OTA_SOURCE_NAME_MAX           15U

#define OTA_WIFI_TCP_PORT             5000U
#define OTA_WIFI_MAX_FRAME_PAYLOAD    4096U
#define OTA_WIFI_THREAD_STACK_SIZE    (8U * 1024U)
#define OTA_WIFI_THREAD_PRIORITY      21U
#define OTA_WIFI_SOCKET_TIMEOUT_MS    15000U

/*
 * Public defaults for a clean checkout. Override these macros from the
 * project configuration, use an ignored ota_config_local.h, or pass an
 * ota_remote_config_t to ota_remote_start().
 */
#ifndef OTA_MQTT_BROKER_URI
#define OTA_MQTT_BROKER_URI           "tcp://127.0.0.1:1883"
#endif
#ifndef OTA_MQTT_USERNAME
#define OTA_MQTT_USERNAME             ""
#endif
#ifndef OTA_MQTT_PASSWORD
#define OTA_MQTT_PASSWORD             ""
#endif

#define OTA_MQTT_JSON_MAX_SIZE        1536U
#define OTA_MQTT_TOPIC_MAX_SIZE       128U
#define OTA_MQTT_PUBLISH_TIMEOUT_MS   5000U
#define OTA_MQTT_RETRY_DELAY_MS       5000U
#define OTA_MQTT_THREAD_STACK_SIZE    (10U * 1024U)
#define OTA_MQTT_THREAD_PRIORITY      20U
#define OTA_MQTT_QUEUE_DEPTH          4U
#define OTA_MQTT_TX_QUEUE_DEPTH       8U
#define OTA_MQTT_TX_THREAD_STACK_SIZE (6U * 1024U)
#define OTA_MQTT_TX_THREAD_PRIORITY   22U
#define OTA_MQTT_KEEPALIVE_SECONDS    120U
#define OTA_MQTT_ROLLOUT_DELAY_LIMIT  86400U

#define OTA_HTTP_HEADER_SIZE          1024U
#define OTA_HTTP_BUFFER_SIZE          2048U
#define OTA_HTTP_TIMEOUT_MS           15000U
#define OTA_HTTP_PROGRESS_STEP        5U

#define OTA_BOOTLOADER_VERSION_TEXT   "V1.0.0"

#endif
#endif
