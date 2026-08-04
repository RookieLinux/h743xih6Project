#include "ota_wifi.h"

#include <rtthread.h>
#include <sal_socket.h>
#include <string.h>
#include <sys/time.h>
#include <wlan_mgnt.h>

#include "ota.h"
#include "ota_config.h"
#include "ota_hash.h"

#define DBG_TAG "app.ota.wifi"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define OTA_WIFI_BEGIN_PAYLOAD_SIZE 12U
#define OTA_WIFI_STATUS_PAYLOAD_SIZE 20U

static rt_thread_t ota_wifi_thread;
static uint8_t ota_wifi_payload[OTA_WIFI_MAX_FRAME_PAYLOAD];

static uint16_t load_le16(const uint8_t *data)
{
    return (uint16_t)data[0] | ((uint16_t)data[1] << 8U);
}

static uint32_t load_le32(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8U) |
           ((uint32_t)data[2] << 16U) |
           ((uint32_t)data[3] << 24U);
}

static void store_le16(uint8_t *data, uint16_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
}

static void store_le32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)value;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)(value >> 16U);
    data[3] = (uint8_t)(value >> 24U);
}

static int socket_receive_exact(int socket, void *buffer, size_t length)
{
    uint8_t *bytes = (uint8_t *)buffer;
    size_t received = 0U;

    while (received < length)
    {
        int result = sal_recvfrom(socket, bytes + received,
                                  length - received, 0,
                                  RT_NULL, RT_NULL);
        if (result == 0)
        {
            return OTA_ERROR_NETWORK;
        }
        if (result < 0)
        {
            return OTA_ERROR_TIMEOUT;
        }
        received += (size_t)result;
    }
    return OTA_OK;
}

static int socket_send_all(int socket, const void *buffer, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)buffer;
    size_t sent = 0U;

    while (sent < length)
    {
        int result = sal_sendto(socket, bytes + sent, length - sent,
                                0, RT_NULL, 0);
        if (result <= 0)
        {
            return OTA_ERROR_NETWORK;
        }
        sent += (size_t)result;
    }
    return OTA_OK;
}

static int send_response(int socket,
                         uint16_t request_type,
                         uint32_t sequence,
                         int result,
                         const void *payload,
                         uint32_t payload_length)
{
    uint8_t header[OTA_WIFI_FRAME_HEADER_SIZE];
    uint32_t payload_crc = 0U;

    if ((payload != RT_NULL) && (payload_length != 0U))
    {
        payload_crc = ota_crc32_calculate(payload, payload_length);
    }
    store_le32(&header[0], OTA_WIFI_PROTOCOL_MAGIC);
    store_le16(&header[4], OTA_WIFI_PROTOCOL_VERSION);
    store_le16(&header[6],
               (uint16_t)(request_type | OTA_WIFI_MSG_RESPONSE_BIT));
    store_le32(&header[8], sequence);
    store_le32(&header[12], (uint32_t)result);
    store_le32(&header[16], payload_length);
    store_le32(&header[20], payload_crc);

    if (socket_send_all(socket, header, sizeof(header)) != OTA_OK)
    {
        return OTA_ERROR_NETWORK;
    }
    if ((payload_length != 0U) &&
        (socket_send_all(socket, payload, payload_length) != OTA_OK))
    {
        return OTA_ERROR_NETWORK;
    }
    return OTA_OK;
}

static int send_status_response(int socket,
                                uint16_t request_type,
                                uint32_t sequence)
{
    ota_status_t status;
    uint8_t payload[OTA_WIFI_STATUS_PAYLOAD_SIZE];

    ota_get_status(&status);
    store_le32(&payload[0], (uint32_t)status.state);
    store_le32(&payload[4], (uint32_t)status.last_error);
    store_le32(&payload[8], status.package_size);
    store_le32(&payload[12], status.received_size);
    store_le32(&payload[16], status.firmware_version);
    return send_response(socket, request_type, sequence, OTA_OK,
                         payload, sizeof(payload));
}

static void handle_client(int socket)
{
    uint8_t header[OTA_WIFI_FRAME_HEADER_SIZE];
    uint32_t begin_flags = 0U;
    int receiving = 0;

    for (;;)
    {
        uint16_t version;
        uint16_t type;
        uint32_t sequence;
        uint32_t offset;
        uint32_t payload_length;
        uint32_t payload_crc;
        int result;
        int reboot_after_response = 0;

        result = socket_receive_exact(socket, header, sizeof(header));
        if (result != OTA_OK)
        {
            break;
        }

        version = load_le16(&header[4]);
        type = load_le16(&header[6]);
        sequence = load_le32(&header[8]);
        offset = load_le32(&header[12]);
        payload_length = load_le32(&header[16]);
        payload_crc = load_le32(&header[20]);

        if ((load_le32(&header[0]) != OTA_WIFI_PROTOCOL_MAGIC) ||
            (version != OTA_WIFI_PROTOCOL_VERSION) ||
            ((type & OTA_WIFI_MSG_RESPONSE_BIT) != 0U) ||
            (payload_length > sizeof(ota_wifi_payload)))
        {
            send_response(socket, type, sequence, OTA_ERROR_PROTOCOL,
                          RT_NULL, 0U);
            break;
        }

        if (payload_length != 0U)
        {
            result = socket_receive_exact(socket, ota_wifi_payload,
                                          payload_length);
            if (result != OTA_OK)
            {
                break;
            }
            if (ota_crc32_calculate(ota_wifi_payload, payload_length) !=
                payload_crc)
            {
                send_response(socket, type, sequence,
                              OTA_ERROR_TRANSFER_CRC, RT_NULL, 0U);
                continue;
            }
        }
        else if (payload_crc != 0U)
        {
            send_response(socket, type, sequence, OTA_ERROR_PROTOCOL,
                          RT_NULL, 0U);
            continue;
        }

        switch (type)
        {
        case OTA_WIFI_MSG_QUERY:
            if (payload_length != 0U)
            {
                result = OTA_ERROR_PROTOCOL;
                break;
            }
            if (send_status_response(socket, type, sequence) != OTA_OK)
            {
                return;
            }
            continue;

        case OTA_WIFI_MSG_BEGIN:
            if (payload_length != OTA_WIFI_BEGIN_PAYLOAD_SIZE)
            {
                result = OTA_ERROR_PROTOCOL;
                break;
            }
            begin_flags = load_le32(&ota_wifi_payload[8]);
            result = ota_begin("wifi",
                               load_le32(&ota_wifi_payload[0]),
                               load_le32(&ota_wifi_payload[4]));
            receiving = result == OTA_OK;
            break;

        case OTA_WIFI_MSG_DATA:
            if (!receiving || (payload_length == 0U))
            {
                result = OTA_ERROR_STATE;
                break;
            }
            result = ota_write(offset, ota_wifi_payload, payload_length);
            break;

        case OTA_WIFI_MSG_END:
            if (!receiving || (payload_length != 0U))
            {
                result = OTA_ERROR_STATE;
                break;
            }
            result = ota_finish();
            receiving = 0;
            reboot_after_response =
                (result == OTA_OK) &&
                ((begin_flags & OTA_WIFI_BEGIN_FLAG_REBOOT) != 0U);
            break;

        case OTA_WIFI_MSG_ABORT:
            if (payload_length != 0U)
            {
                result = OTA_ERROR_PROTOCOL;
                break;
            }
            ota_abort();
            receiving = 0;
            result = OTA_OK;
            break;

        case OTA_WIFI_MSG_REBOOT:
            if (payload_length != 0U)
            {
                result = OTA_ERROR_PROTOCOL;
                break;
            }
            {
                ota_status_t status;
                ota_get_status(&status);
                result = status.state == OTA_STATE_READY
                             ? OTA_OK
                             : OTA_ERROR_STATE;
                reboot_after_response = result == OTA_OK;
            }
            break;

        default:
            result = OTA_ERROR_PROTOCOL;
            break;
        }

        if (send_response(socket, type, sequence, result,
                          RT_NULL, 0U) != OTA_OK)
        {
            break;
        }
        if (reboot_after_response)
        {
            ota_reboot_to_install(500U);
            return;
        }
    }

    if (receiving)
    {
        ota_abort();
        LOG_W("client disconnected; incomplete download invalidated");
    }
}

static int create_listener(void)
{
    struct sockaddr_in address;
    int listener;
    int reuse = 1;

    listener = sal_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener < 0)
    {
        return -1;
    }
    sal_setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                   &reuse, sizeof(reuse));

    memset(&address, 0, sizeof(address));
    address.sin_len = sizeof(address);
    address.sin_family = AF_INET;
    address.sin_port = htons(OTA_WIFI_TCP_PORT);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if ((sal_bind(listener, (struct sockaddr *)&address,
                  sizeof(address)) != 0) ||
        (sal_listen(listener, 1) != 0))
    {
        sal_closesocket(listener);
        return -1;
    }
    return listener;
}

static void ota_wifi_entry(void *parameter)
{
    (void)parameter;

    for (;;)
    {
        int listener;

        while (!rt_wlan_is_ready())
        {
            rt_thread_mdelay(1000);
        }

        listener = create_listener();
        if (listener < 0)
        {
            LOG_E("cannot listen on TCP port %u",
                  (unsigned)OTA_WIFI_TCP_PORT);
            rt_thread_mdelay(2000);
            continue;
        }
        LOG_I("OTA TCP server listening on port %u",
              (unsigned)OTA_WIFI_TCP_PORT);

        while (rt_wlan_is_ready())
        {
            struct sockaddr_in peer;
            socklen_t peer_length = sizeof(peer);
            struct timeval timeout;
            int client = sal_accept(listener,
                                    (struct sockaddr *)&peer,
                                    &peer_length);
            if (client < 0)
            {
                break;
            }

            timeout.tv_sec = OTA_WIFI_SOCKET_TIMEOUT_MS / 1000U;
            timeout.tv_usec =
                (OTA_WIFI_SOCKET_TIMEOUT_MS % 1000U) * 1000U;
            sal_setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           &timeout, sizeof(timeout));
            sal_setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                           &timeout, sizeof(timeout));
            LOG_I("OTA client connected");
            handle_client(client);
            sal_closesocket(client);
            LOG_I("OTA client closed");
        }
        sal_closesocket(listener);
    }
}

int ota_wifi_server_start(void)
{
    int result = ota_service_init();

    if (result != OTA_OK)
    {
        return result;
    }
    if (ota_wifi_thread != RT_NULL)
    {
        return OTA_OK;
    }

    ota_wifi_thread = rt_thread_create(
        "ota_wifi", ota_wifi_entry, RT_NULL,
        OTA_WIFI_THREAD_STACK_SIZE,
        OTA_WIFI_THREAD_PRIORITY, 10U);
    if (ota_wifi_thread == RT_NULL)
    {
        return OTA_ERROR_NO_MEMORY;
    }
    rt_thread_startup(ota_wifi_thread);
    return OTA_OK;
}
