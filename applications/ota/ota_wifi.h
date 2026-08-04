#ifndef APP_OTA_WIFI_H
#define APP_OTA_WIFI_H

#include <stdint.h>

#define OTA_WIFI_PROTOCOL_MAGIC    0x3141544FUL /* "OTA1", little endian */
#define OTA_WIFI_PROTOCOL_VERSION  1U
#define OTA_WIFI_FRAME_HEADER_SIZE 24U

typedef enum
{
    OTA_WIFI_MSG_QUERY = 1,
    OTA_WIFI_MSG_BEGIN = 2,
    OTA_WIFI_MSG_DATA = 3,
    OTA_WIFI_MSG_END = 4,
    OTA_WIFI_MSG_ABORT = 5,
    OTA_WIFI_MSG_REBOOT = 6,
    OTA_WIFI_MSG_RESPONSE_BIT = 0x8000
} ota_wifi_message_type_t;

#define OTA_WIFI_BEGIN_FLAG_REBOOT  (1UL << 0)

int ota_wifi_server_start(void);

#endif
