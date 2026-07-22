#include "lvww_rw007.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <drivers/rtc.h>
#include <sal_netdb.h>
#include <sal_socket.h>
#include <sys/time.h>
#include <wlan_mgnt.h>

#define LVWW_RW007_QUEUE_DEPTH       8
/* DNS/SAL/newlib run synchronously in this worker.  A 6 KiB stack can
 * underflow into the dynamically allocated rt_thread control block while
 * resolving the first host after Wi-Fi comes online. */
#define LVWW_RW007_STACK_SIZE        (16 * 1024)
#define LVWW_RW007_SCAN_TIMEOUT_MS   12000
#define LVWW_RW007_READY_TIMEOUT_MS  15000
#define LVWW_RW007_SOCKET_TIMEOUT_MS 8000
#define LVWW_RW007_HTTP_MAX          (12 * 1024)
#define LVWW_RW007_KV_SLOTS          3
#define LVWW_RW007_KV_SIZE           1536

#define LVWW_GEOCODING_HOST "geocoding-api.open-meteo.com"
#define LVWW_FORECAST_HOST  "api.open-meteo.com"

typedef enum
{
    RW_CMD_SCAN = 0,
    RW_CMD_CONNECT,
    RW_CMD_DISCONNECT,
    RW_CMD_CITY,
    RW_CMD_WEATHER,
    RW_CMD_TIME,
    RW_CMD_STOP
} lvww_rw_cmd_type_t;

typedef struct
{
    lvww_rw_cmd_type_t type;
    uint32_t request_id;
    union
    {
        lvww_wifi_credentials_t credentials;
        lvww_city_t city;
        char query[LVWW_CITY_NAME_MAX_LEN + 1];
    } data;
} lvww_rw_cmd_t;

typedef struct
{
    char key[24];
    uint8_t data[LVWW_RW007_KV_SIZE];
    rt_size_t length;
    rt_bool_t used;
} lvww_rw_kv_t;

struct lvww_rw007
{
    lvww_ctx_t *ctx;
    rt_mq_t queue;
    rt_thread_t worker;
    rt_sem_t stopped;
    rt_sem_t scan_done;
    rt_mutex_t lock;
    rt_bool_t running;
    rt_bool_t disconnecting;
    lvww_wifi_ap_t scan_results[LVWW_MAX_WIFI_RESULTS];
    uint16_t scan_count;
    uint32_t latest[LVWW_OP_STORAGE + 1];
    uint32_t cancelled[LVWW_OP_STORAGE + 1];
    lvww_rw_kv_t kv[LVWW_RW007_KV_SLOTS];
};

static lvww_operation_t rw_operation(lvww_rw_cmd_type_t type)
{
    switch (type)
    {
    case RW_CMD_SCAN: return LVWW_OP_WIFI_SCAN;
    case RW_CMD_CONNECT: return LVWW_OP_WIFI_CONNECT;
    case RW_CMD_DISCONNECT: return LVWW_OP_WIFI_DISCONNECT;
    case RW_CMD_CITY: return LVWW_OP_CITY_SEARCH;
    case RW_CMD_WEATHER: return LVWW_OP_WEATHER_FETCH;
    case RW_CMD_TIME: return LVWW_OP_TIME_SYNC;
    default: return LVWW_OP_NONE;
    }
}

static rt_bool_t rw_request_current(lvww_rw007_t *backend,
                                    lvww_operation_t operation,
                                    uint32_t request_id)
{
    rt_bool_t current;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    current = backend->running &&
              operation <= LVWW_OP_STORAGE &&
              backend->latest[operation] == request_id &&
              backend->cancelled[operation] != request_id;
    rt_mutex_release(backend->lock);
    return current;
}

static lvww_ctx_t *rw_bound_ctx(lvww_rw007_t *backend)
{
    lvww_ctx_t *ctx;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    ctx = backend->ctx;
    rt_mutex_release(backend->lock);
    return ctx;
}

static void rw_post(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd,
                    lvww_event_t *event)
{
    lvww_operation_t operation = rw_operation(cmd->type);
    lvww_ctx_t *ctx;
    if (!rw_request_current(backend, operation, cmd->request_id))
        return;
    ctx = rw_bound_ctx(backend);
    if (!ctx)
        return;
    event->request_id = cmd->request_id;
    lvww_post_event(ctx, event);
}

static void rw_post_error(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd,
                          lvww_operation_t operation, int code,
                          const char *message)
{
    lvww_event_t event;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_ERROR;
    event.data.error.operation = operation;
    event.data.error.code = code;
    rt_strncpy(event.data.error.text, message, LVWW_ERROR_TEXT_MAX_LEN);
    rw_post(backend, cmd, &event);
}

static int ascii_tolower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static const char *mem_find_ci(const char *data, rt_size_t length,
                               const char *needle)
{
    rt_size_t needle_len = rt_strlen(needle);
    rt_size_t i, j;
    if (!needle_len || needle_len > length)
        return RT_NULL;
    for (i = 0; i + needle_len <= length; ++i)
    {
        for (j = 0; j < needle_len; ++j)
            if (ascii_tolower((unsigned char)data[i + j]) !=
                ascii_tolower((unsigned char)needle[j]))
                break;
        if (j == needle_len)
            return data + i;
    }
    return RT_NULL;
}

static uint64_t civil_to_epoch(int year, unsigned month, unsigned day,
                               unsigned hour, unsigned minute, unsigned second)
{
    int era;
    unsigned yoe, doy, doe;
    int64_t days;
    year -= month <= 2;
    era = (year >= 0 ? year : year - 399) / 400;
    yoe = (unsigned)(year - era * 400);
    doy = (153u * (month + (month > 2 ? (unsigned)-3 : 9u)) + 2u) / 5u + day - 1u;
    doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    days = (int64_t)era * 146097 + (int64_t)doe - 719468;
    if (days < 0)
        return 0;
    return (uint64_t)days * 86400u + hour * 3600u + minute * 60u + second;
}

static uint64_t parse_http_date(const char *headers, rt_size_t length)
{
    static const char *months[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    const char *date = mem_find_ci(headers, length, "\r\nDate:");
    char month_text[4] = {0};
    int day, year, hour, minute, second, month;
    if (!date)
        return 0;
    date += 7;
    while (date < headers + length && (*date == ' ' || *date == '\t'))
        ++date;
    if (sscanf(date, "%*3s, %d %3s %d %d:%d:%d GMT",
               &day, month_text, &year, &hour, &minute, &second) != 6)
        return 0;
    for (month = 0; month < 12; ++month)
        if (rt_memcmp(month_text, months[month], 3) == 0)
            return civil_to_epoch(year, (unsigned)month + 1u, (unsigned)day,
                                  (unsigned)hour, (unsigned)minute,
                                  (unsigned)second);
    return 0;
}

static int socket_connect_host(const char *host, const char *service, int type)
{
    struct addrinfo hints;
    struct addrinfo *result = RT_NULL;
    struct addrinfo *item;
    struct timeval timeout;
    int sock = -1;
    rt_memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = type;
    if (sal_getaddrinfo(host, service, &hints, &result) != 0 || !result)
        return -1;
    timeout.tv_sec = LVWW_RW007_SOCKET_TIMEOUT_MS / 1000;
    timeout.tv_usec = (LVWW_RW007_SOCKET_TIMEOUT_MS % 1000) * 1000;
    for (item = result; item; item = item->ai_next)
    {
        sock = sal_socket(item->ai_family, item->ai_socktype, item->ai_protocol);
        if (sock < 0)
            continue;
        sal_setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
        sal_setsockopt(sock, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
        if (sal_connect(sock, item->ai_addr, item->ai_addrlen) == 0)
            break;
        sal_closesocket(sock);
        sock = -1;
    }
    sal_freeaddrinfo(result);
    return sock;
}

static int socket_send_all(int sock, const char *data, rt_size_t length)
{
    rt_size_t sent = 0;
    while (sent < length)
    {
        int rc = sal_sendto(sock, data + sent, length - sent, 0, RT_NULL, RT_NULL);
        if (rc <= 0)
            return -RT_ERROR;
        sent += (rt_size_t)rc;
    }
    return RT_EOK;
}

static int hex_value(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int decode_chunked(char *body, rt_size_t length, rt_size_t *decoded_length)
{
    char *read = body;
    char *write = body;
    char *end = body + length;
    while (read < end)
    {
        unsigned long chunk = 0;
        int digit = 0;
        while (read < end && *read != '\r')
        {
            int value;
            if (*read == ';')
            {
                while (read < end && *read != '\r') ++read;
                break;
            }
            value = hex_value(*read++);
            if (value < 0)
                return -RT_ERROR;
            chunk = chunk * 16u + (unsigned)value;
            digit = 1;
        }
        if (!digit || read + 1 >= end || read[0] != '\r' || read[1] != '\n')
            return -RT_ERROR;
        read += 2;
        if (chunk == 0)
        {
            *write = '\0';
            *decoded_length = (rt_size_t)(write - body);
            return RT_EOK;
        }
        if ((rt_size_t)(end - read) < chunk + 2u)
            return -RT_ERROR;
        rt_memmove(write, read, (rt_size_t)chunk);
        write += chunk;
        read += chunk;
        if (read[0] != '\r' || read[1] != '\n')
            return -RT_ERROR;
        read += 2;
    }
    return -RT_ERROR;
}

static int http_get_json(const char *host, const char *path,
                         char **json, uint64_t *server_time)
{
    char request[768];
    char *response;
    char *header_end;
    char *body;
    const char *length_header;
    int sock;
    int status = 0;
    int rc;
    rt_size_t used = 0;
    rt_size_t header_length;
    rt_size_t body_length;
    rt_bool_t chunked;
    long content_length = -1;

    if (json) *json = RT_NULL;
    if (server_time) *server_time = 0;
    sock = socket_connect_host(host, "80", SOCK_STREAM);
    if (sock < 0)
        return -RT_ERROR;
    rc = rt_snprintf(request, sizeof(request),
                     "GET %s HTTP/1.1\r\n"
                     "Host: %s\r\n"
                     "User-Agent: lvww-rw007/1.0\r\n"
                     "Accept: application/json\r\n"
                     "Accept-Encoding: identity\r\n"
                     "Connection: close\r\n\r\n",
                     path, host);
    if (rc <= 0 || (rt_size_t)rc >= sizeof(request) ||
        socket_send_all(sock, request, (rt_size_t)rc) != RT_EOK)
    {
        sal_closesocket(sock);
        return -RT_ERROR;
    }

    response = (char *)rt_malloc(LVWW_RW007_HTTP_MAX + 1u);
    if (!response)
    {
        sal_closesocket(sock);
        return -RT_ENOMEM;
    }
    while (used < LVWW_RW007_HTTP_MAX)
    {
        rc = sal_recvfrom(sock, response + used, LVWW_RW007_HTTP_MAX - used,
                          0, RT_NULL, RT_NULL);
        if (rc == 0)
            break;
        if (rc < 0)
        {
            rt_free(response);
            sal_closesocket(sock);
            return -RT_ETIMEOUT;
        }
        used += (rt_size_t)rc;
    }
    sal_closesocket(sock);
    if (used == 0 || used >= LVWW_RW007_HTTP_MAX)
    {
        rt_free(response);
        return -RT_EFULL;
    }
    response[used] = '\0';
    header_end = strstr(response, "\r\n\r\n");
    if (!header_end || sscanf(response, "HTTP/%*u.%*u %d", &status) != 1 ||
        status < 200 || status >= 300)
    {
        rt_free(response);
        return -RT_ERROR;
    }
    header_length = (rt_size_t)(header_end + 4 - response);
    body = header_end + 4;
    body_length = used - header_length;
    if (server_time)
        *server_time = parse_http_date(response, header_length);
    chunked = mem_find_ci(response, header_length,
                          "Transfer-Encoding: chunked") != RT_NULL;
    length_header = mem_find_ci(response, header_length, "Content-Length:");
    if (length_header)
        content_length = strtol(length_header + 15, RT_NULL, 10);
    if (chunked)
    {
        if (decode_chunked(body, body_length, &body_length) != RT_EOK)
        {
            rt_free(response);
            return -RT_ERROR;
        }
    }
    else if (content_length >= 0)
    {
        if ((rt_size_t)content_length > body_length)
        {
            rt_free(response);
            return -RT_ERROR;
        }
        body_length = (rt_size_t)content_length;
        body[body_length] = '\0';
    }
    else
    {
        body[body_length] = '\0';
    }
    rt_memmove(response, body, body_length + 1u);
    if (json)
        *json = response;
    else
        rt_free(response);
    return RT_EOK;
}

static const char *json_skip_ws(const char *cursor, const char *end)
{
    while (cursor < end && isspace((unsigned char)*cursor)) ++cursor;
    return cursor;
}

static const char *json_skip_string(const char *cursor, const char *end)
{
    if (cursor >= end || *cursor != '"') return RT_NULL;
    ++cursor;
    while (cursor < end)
    {
        if (*cursor == '\\')
        {
            cursor += 2;
            continue;
        }
        if (*cursor == '"') return cursor + 1;
        ++cursor;
    }
    return RT_NULL;
}

static const char *json_skip_value(const char *cursor, const char *end)
{
    char open, close;
    int depth;
    cursor = json_skip_ws(cursor, end);
    if (cursor >= end) return RT_NULL;
    if (*cursor == '"') return json_skip_string(cursor, end);
    if (*cursor != '{' && *cursor != '[')
    {
        while (cursor < end && *cursor != ',' && *cursor != '}' &&
               *cursor != ']' && !isspace((unsigned char)*cursor))
            ++cursor;
        return cursor;
    }
    open = *cursor;
    close = open == '{' ? '}' : ']';
    depth = 1;
    ++cursor;
    while (cursor < end && depth)
    {
        if (*cursor == '"')
        {
            cursor = json_skip_string(cursor, end);
            if (!cursor) return RT_NULL;
            continue;
        }
        if (*cursor == open) ++depth;
        else if (*cursor == close) --depth;
        ++cursor;
    }
    return depth == 0 ? cursor : RT_NULL;
}

static rt_bool_t json_find_member(const char *object, const char *end,
                                  const char *key, const char **value,
                                  const char **value_end)
{
    const char *cursor;
    rt_size_t key_len = rt_strlen(key);
    if (!object || object >= end || *object != '{') return RT_FALSE;
    cursor = object + 1;
    while ((cursor = json_skip_ws(cursor, end)) < end && *cursor != '}')
    {
        const char *key_start;
        const char *key_finish;
        const char *item_end;
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        if (*cursor != '"') return RT_FALSE;
        key_start = cursor + 1;
        key_finish = json_skip_string(cursor, end);
        if (!key_finish) return RT_FALSE;
        cursor = json_skip_ws(key_finish, end);
        if (cursor >= end || *cursor++ != ':') return RT_FALSE;
        cursor = json_skip_ws(cursor, end);
        item_end = json_skip_value(cursor, end);
        if (!item_end) return RT_FALSE;
        if ((rt_size_t)(key_finish - key_start - 1) == key_len &&
            rt_memcmp(key_start, key, key_len) == 0)
        {
            *value = cursor;
            *value_end = item_end;
            return RT_TRUE;
        }
        cursor = item_end;
    }
    return RT_FALSE;
}

static rt_bool_t json_array_item(const char *array, const char *end,
                                 unsigned index, const char **value,
                                 const char **value_end)
{
    const char *cursor;
    unsigned current = 0;
    if (!array || array >= end || *array != '[') return RT_FALSE;
    cursor = array + 1;
    while ((cursor = json_skip_ws(cursor, end)) < end && *cursor != ']')
    {
        const char *item_end;
        if (*cursor == ',')
        {
            ++cursor;
            continue;
        }
        item_end = json_skip_value(cursor, end);
        if (!item_end) return RT_FALSE;
        if (current++ == index)
        {
            *value = cursor;
            *value_end = item_end;
            return RT_TRUE;
        }
        cursor = item_end;
    }
    return RT_FALSE;
}

static unsigned json_hex4(const char *text, rt_bool_t *valid)
{
    unsigned value = 0;
    int i;
    *valid = RT_FALSE;
    for (i = 0; i < 4; ++i)
    {
        int digit = hex_value(text[i]);
        if (digit < 0) return 0;
        value = value * 16u + (unsigned)digit;
    }
    *valid = RT_TRUE;
    return value;
}

static void json_put_bytes(char *output, rt_size_t capacity, rt_size_t *used,
                           const unsigned char *bytes, unsigned count)
{
    if (*used + count >= capacity)
    {
        /* Mark the buffer full without ever writing half of a UTF-8 glyph. */
        *used = capacity - 1u;
        return;
    }
    rt_memcpy(output + *used, bytes, count);
    *used += count;
}

static void json_put_utf8(char *output, rt_size_t capacity, rt_size_t *used,
                          unsigned codepoint)
{
    unsigned char bytes[4];
    unsigned count;
    if (codepoint > 0x10FFFFu ||
        (codepoint >= 0xD800u && codepoint <= 0xDFFFu))
        codepoint = 0xFFFDu;
    if (codepoint < 0x80u)
    {
        bytes[0] = (unsigned char)codepoint;
        count = 1;
    }
    else if (codepoint < 0x800u)
    {
        bytes[0] = (unsigned char)(0xC0u | (codepoint >> 6));
        bytes[1] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        count = 2;
    }
    else if (codepoint < 0x10000u)
    {
        bytes[0] = (unsigned char)(0xE0u | (codepoint >> 12));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[2] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        count = 3;
    }
    else
    {
        bytes[0] = (unsigned char)(0xF0u | (codepoint >> 18));
        bytes[1] = (unsigned char)(0x80u | ((codepoint >> 12) & 0x3Fu));
        bytes[2] = (unsigned char)(0x80u | ((codepoint >> 6) & 0x3Fu));
        bytes[3] = (unsigned char)(0x80u | (codepoint & 0x3Fu));
        count = 4;
    }
    json_put_bytes(output, capacity, used, bytes, count);
}

static rt_bool_t json_copy_string(const char *value, const char *end,
                                  char *output, rt_size_t capacity)
{
    const char *cursor;
    rt_size_t used = 0;
    if (!value || value >= end || *value != '"' || capacity == 0)
        return RT_FALSE;
    cursor = value + 1;
    while (cursor < end && *cursor != '"')
    {
        unsigned codepoint;
        rt_bool_t valid;
        char c = *cursor++;
        if (c != '\\')
        {
            const unsigned char *sequence =
                (const unsigned char *)(cursor - 1);
            unsigned char lead = sequence[0];
            unsigned count = 1;
            unsigned i;
            if ((lead & 0xE0u) == 0xC0u) count = 2;
            else if ((lead & 0xF0u) == 0xE0u) count = 3;
            else if ((lead & 0xF8u) == 0xF0u) count = 4;
            else if (lead >= 0x80u) count = 0;
            if (!count || cursor + count - 1u > end)
            {
                json_put_utf8(output, capacity, &used, 0xFFFDu);
                continue;
            }
            for (i = 1; i < count; ++i)
            {
                if ((sequence[i] & 0xC0u) != 0x80u)
                    break;
            }
            if (i != count)
            {
                json_put_utf8(output, capacity, &used, 0xFFFDu);
                continue;
            }
            json_put_bytes(output, capacity, &used, sequence, count);
            cursor += count - 1u;
            continue;
        }
        if (cursor >= end) return RT_FALSE;
        c = *cursor++;
        if (c == 'u' && cursor + 4 <= end)
        {
            codepoint = json_hex4(cursor, &valid);
            if (!valid) return RT_FALSE;
            cursor += 4;
            if (codepoint >= 0xD800u && codepoint <= 0xDBFFu &&
                cursor + 6 <= end && cursor[0] == '\\' && cursor[1] == 'u')
            {
                unsigned low = json_hex4(cursor + 2, &valid);
                if (valid && low >= 0xDC00u && low <= 0xDFFFu)
                {
                    codepoint = 0x10000u +
                                ((codepoint - 0xD800u) << 10) +
                                (low - 0xDC00u);
                    cursor += 6;
                }
            }
            json_put_utf8(output, capacity, &used, codepoint);
        }
        else
        {
            if (c == 'n') c = '\n';
            else if (c == 'r') c = '\r';
            else if (c == 't') c = '\t';
            else if (c == 'b') c = '\b';
            else if (c == 'f') c = '\f';
            json_put_bytes(output, capacity, &used,
                           (const unsigned char *)&c, 1);
        }
    }
    output[used] = '\0';
    return cursor < end && *cursor == '"';
}

static rt_bool_t json_member_string(const char *object, const char *end,
                                    const char *key, char *output,
                                    rt_size_t capacity)
{
    const char *value, *value_end;
    return json_find_member(object, end, key, &value, &value_end) &&
           json_copy_string(value, value_end, output, capacity);
}

static rt_bool_t json_member_number(const char *object, const char *end,
                                    const char *key, double *number)
{
    const char *value, *value_end;
    char text[32];
    rt_size_t length;
    if (!json_find_member(object, end, key, &value, &value_end)) return RT_FALSE;
    length = (rt_size_t)(value_end - value);
    if (length == 0 || length >= sizeof(text)) return RT_FALSE;
    rt_memcpy(text, value, length);
    text[length] = '\0';
    *number = strtod(text, RT_NULL);
    return RT_TRUE;
}

static rt_bool_t json_member_array_number(const char *object, const char *end,
                                          const char *key, unsigned index,
                                          double *number)
{
    const char *array, *array_end, *value, *value_end;
    char text[32];
    rt_size_t length;
    if (!json_find_member(object, end, key, &array, &array_end) ||
        !json_array_item(array, array_end, index, &value, &value_end))
        return RT_FALSE;
    length = (rt_size_t)(value_end - value);
    if (length == 0 || length >= sizeof(text)) return RT_FALSE;
    rt_memcpy(text, value, length);
    text[length] = '\0';
    *number = strtod(text, RT_NULL);
    return RT_TRUE;
}

static int url_encode(const char *input, char *output, rt_size_t capacity)
{
    static const char hex[] = "0123456789ABCDEF";
    rt_size_t used = 0;
    while (*input)
    {
        unsigned char c = (unsigned char)*input++;
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
        {
            if (used + 1u >= capacity) return -RT_EFULL;
            output[used++] = (char)c;
        }
        else
        {
            if (used + 3u >= capacity) return -RT_EFULL;
            output[used++] = '%';
            output[used++] = hex[c >> 4];
            output[used++] = hex[c & 0x0Fu];
        }
    }
    output[used] = '\0';
    return RT_EOK;
}

static lvww_weather_code_t map_weather_code(int code)
{
    if (code == 0) return LVWW_WEATHER_CLEAR;
    if (code == 1 || code == 2) return LVWW_WEATHER_PARTLY_CLOUDY;
    if (code == 3) return LVWW_WEATHER_CLOUDY;
    if (code == 45 || code == 48) return LVWW_WEATHER_FOG;
    if ((code >= 51 && code <= 57)) return LVWW_WEATHER_DRIZZLE;
    if ((code >= 61 && code <= 67) || (code >= 80 && code <= 82))
        return LVWW_WEATHER_RAIN;
    if ((code >= 71 && code <= 77) || code == 85 || code == 86)
        return LVWW_WEATHER_SNOW;
    if (code >= 95 && code <= 99) return LVWW_WEATHER_STORM;
    return LVWW_WEATHER_UNKNOWN;
}

static void rw_scan_report_cb(int event, struct rt_wlan_buff *buff, void *parameter)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)parameter;
    const struct rt_wlan_info *info;
    uint16_t i;
    (void)event;
    if (!backend || !buff || !buff->data ||
        buff->len != sizeof(struct rt_wlan_info))
        return;
    info = (const struct rt_wlan_info *)buff->data;
    if (!info->ssid.len || info->ssid.len > LVWW_SSID_MAX_LEN)
        return;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    for (i = 0; i < backend->scan_count; ++i)
    {
        if (rt_strlen(backend->scan_results[i].ssid) == info->ssid.len &&
            rt_memcmp(backend->scan_results[i].ssid, info->ssid.val,
                      info->ssid.len) == 0)
        {
            if (info->rssi > backend->scan_results[i].rssi)
                backend->scan_results[i].rssi = info->rssi;
            rt_mutex_release(backend->lock);
            return;
        }
    }
    if (backend->scan_count < LVWW_MAX_WIFI_RESULTS)
    {
        lvww_wifi_ap_t *ap = &backend->scan_results[backend->scan_count++];
        rt_memset(ap, 0, sizeof(*ap));
        rt_memcpy(ap->ssid, info->ssid.val, info->ssid.len);
        ap->ssid[info->ssid.len] = '\0';
        ap->rssi = info->rssi;
        ap->secure = info->security != SECURITY_OPEN;
    }
    rt_mutex_release(backend->lock);
}

static void rw_scan_done_cb(int event, struct rt_wlan_buff *buff, void *parameter)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)parameter;
    (void)event;
    (void)buff;
    if (backend) rt_sem_release(backend->scan_done);
}

static void rw_disconnected_cb(int event, struct rt_wlan_buff *buff, void *parameter)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)parameter;
    lvww_event_t *state;
    lvww_ctx_t *ctx;
    rt_bool_t suppress;
    (void)event;
    (void)buff;
    if (!backend) return;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    suppress = backend->disconnecting || !backend->running;
    ctx = backend->ctx;
    rt_mutex_release(backend->lock);
    if (suppress || !ctx) return;

    /* This callback runs in the 2 KiB WLAN workqueue thread.  lvww_event_t is
     * almost 2 KiB because its union contains the complete city-result list,
     * so placing one on this stack immediately trips RT-Thread's overflow
     * guard.  Allocate the copied message temporarily from the heap instead. */
    state = (lvww_event_t *)rt_calloc(1, sizeof(*state));
    if (!state) return;
    state->type = LVWW_EVT_WIFI_STATE;
    state->request_id = 0;
    state->data.wifi_state.state = LVWW_WIFI_OFFLINE;
    state->data.wifi_state.reason = LVWW_WIFI_REASON_DROPPED;
    lvww_post_event(ctx, state);
    rt_free(state);
}

static void rw_do_scan(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    lvww_event_t event;
    while (rt_sem_take(backend->scan_done, 0) == RT_EOK) {}
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->scan_count = 0;
    rt_memset(backend->scan_results, 0, sizeof(backend->scan_results));
    rt_mutex_release(backend->lock);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_REPORT, rw_scan_report_cb, backend);
    rt_wlan_register_event_handler(RT_WLAN_EVT_SCAN_DONE, rw_scan_done_cb, backend);
    if (rt_wlan_scan() != RT_EOK ||
        rt_sem_take(backend->scan_done,
                    rt_tick_from_millisecond(LVWW_RW007_SCAN_TIMEOUT_MS)) != RT_EOK)
    {
        rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
        rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
        rw_post_error(backend, cmd, LVWW_OP_WIFI_SCAN, -RT_ETIMEOUT,
                      "Wi-Fi 扫描超时");
        return;
    }
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_SCAN_RESULT;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    event.data.wifi_scan.count = backend->scan_count;
    rt_memcpy(event.data.wifi_scan.items, backend->scan_results,
              backend->scan_count * sizeof(backend->scan_results[0]));
    rt_mutex_release(backend->lock);
    rw_post(backend, cmd, &event);
}

static void rw_do_connect(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    lvww_event_t event;
    rt_err_t rc;
    rt_tick_t started;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    event.data.wifi_state.state = LVWW_WIFI_CONNECTING;
    rt_strncpy(event.data.wifi_state.ssid, cmd->data.credentials.ssid,
               LVWW_SSID_MAX_LEN);
    rw_post(backend, cmd, &event);

    /* A manual connection attempt must own the retry policy.  Otherwise a
     * CONNECT_FAIL starts the WLAN manager's auto-connect timer while this
     * worker is still unwinding rt_wlan_connect(), and both paths contend for
     * the same management mutex.  Enable auto reconnect again only after the
     * new credentials have connected successfully. */
    rt_wlan_config_autoreconnect(RT_FALSE);
    rc = rt_wlan_connect(cmd->data.credentials.ssid,
                         cmd->data.credentials.secure ?
                         cmd->data.credentials.password : RT_NULL);
    started = rt_tick_get();
    while (rc == RT_EOK && !rt_wlan_is_ready() &&
           (uint32_t)((rt_tick_get() - started) * 1000u / RT_TICK_PER_SECOND) <
               LVWW_RW007_READY_TIMEOUT_MS)
        rt_thread_mdelay(100);

    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    rt_strncpy(event.data.wifi_state.ssid, cmd->data.credentials.ssid,
               LVWW_SSID_MAX_LEN);
    if (rc == RT_EOK && rt_wlan_is_ready())
    {
        event.data.wifi_state.state = LVWW_WIFI_ONLINE;
        event.data.wifi_state.reason = LVWW_WIFI_REASON_NONE;
        rt_wlan_config_autoreconnect(RT_TRUE);
    }
    else
    {
        event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
        event.data.wifi_state.reason = rc == RT_EOK ?
                                       LVWW_WIFI_REASON_TIMEOUT :
                                       (cmd->data.credentials.secure ?
                                        LVWW_WIFI_REASON_AUTH :
                                        LVWW_WIFI_REASON_OTHER);
    }
    rw_post(backend, cmd, &event);
}

static void rw_do_disconnect(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    lvww_event_t event;
    rt_err_t rc;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->disconnecting = RT_TRUE;
    rt_mutex_release(backend->lock);
    rt_wlan_config_autoreconnect(RT_FALSE);
    rc = rt_wlan_disconnect();
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->disconnecting = RT_FALSE;
    rt_mutex_release(backend->lock);
    if (rc != RT_EOK)
    {
        rw_post_error(backend, cmd, LVWW_OP_WIFI_DISCONNECT, rc,
                      "Wi-Fi 断开失败");
        return;
    }
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
    event.data.wifi_state.reason = LVWW_WIFI_REASON_NONE;
    rw_post(backend, cmd, &event);
}

static void rw_do_city(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    char encoded[3 * (LVWW_CITY_NAME_MAX_LEN + 1)];
    char path[256];
    char *json = RT_NULL;
    const char *json_end;
    const char *results, *results_end;
    lvww_event_t event;
    unsigned index;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_CITY_SEARCH_RESULT;
    event.data.city_search.count = (uint16_t)lvww_city_catalog_search(
        cmd->data.query, event.data.city_search.items, LVWW_MAX_CITY_RESULTS);
    if (event.data.city_search.count)
    {
        rw_post(backend, cmd, &event);
        return;
    }
    if (!rt_wlan_is_ready())
    {
        rw_post_error(backend, cmd, LVWW_OP_CITY_SEARCH, -RT_ERROR,
                      "网络尚未就绪");
        return;
    }
    if (url_encode(cmd->data.query, encoded, sizeof(encoded)) != RT_EOK)
    {
        rw_post_error(backend, cmd, LVWW_OP_CITY_SEARCH, -RT_EINVAL,
                      "城市名称过长");
        return;
    }
    rt_snprintf(path, sizeof(path),
                "/v1/search?name=%s&count=%u&language=zh&format=json",
                encoded, LVWW_MAX_CITY_RESULTS);
    if (http_get_json(LVWW_GEOCODING_HOST, path, &json, RT_NULL) != RT_EOK)
    {
        rw_post_error(backend, cmd, LVWW_OP_CITY_SEARCH, -RT_ERROR,
                      "城市服务请求失败");
        return;
    }
    json_end = json + rt_strlen(json);
    if (!json_find_member(json, json_end, "results", &results, &results_end) ||
        *results != '[')
    {
        rt_free(json);
        rw_post_error(backend, cmd, LVWW_OP_CITY_SEARCH, -RT_ERROR,
                      "城市数据格式错误");
        return;
    }
    for (index = 0; index < LVWW_MAX_CITY_RESULTS; ++index)
    {
        const char *item, *item_end;
        lvww_city_t *city;
        double number;
        if (!json_array_item(results, results_end, index, &item, &item_end))
            break;
        city = &event.data.city_search.items[event.data.city_search.count];
        if (!json_member_number(item, item_end, "id", &number) ||
            !json_member_string(item, item_end, "name", city->name,
                                sizeof(city->name)) ||
            !json_member_number(item, item_end, "latitude", &city->latitude) ||
            !json_member_number(item, item_end, "longitude", &city->longitude))
            continue;
        rt_snprintf(city->id, sizeof(city->id), "%.0f", number);
        json_member_string(item, item_end, "admin1", city->admin,
                           sizeof(city->admin));
        json_member_string(item, item_end, "country", city->country,
                           sizeof(city->country));
        json_member_string(item, item_end, "timezone", city->timezone,
                           sizeof(city->timezone));
        ++event.data.city_search.count;
    }
    rt_free(json);
    rw_post(backend, cmd, &event);
}

static void rw_do_weather(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    char path[640];
    char *json = RT_NULL;
    const char *json_end;
    const char *current, *current_end;
    const char *daily, *daily_end;
    lvww_event_t event;
    double value;
    if (!rt_wlan_is_ready())
    {
        rw_post_error(backend, cmd, LVWW_OP_WEATHER_FETCH, -RT_ERROR,
                      "网络尚未就绪");
        return;
    }
    rt_snprintf(path, sizeof(path),
                "/v1/forecast?latitude=%.6f&longitude=%.6f"
                "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m"
                "&daily=temperature_2m_max,temperature_2m_min"
                "&timezone=auto&forecast_days=1&timeformat=unixtime",
                cmd->data.city.latitude, cmd->data.city.longitude);
    if (http_get_json(LVWW_FORECAST_HOST, path, &json, RT_NULL) != RT_EOK)
    {
        rw_post_error(backend, cmd, LVWW_OP_WEATHER_FETCH, -RT_ERROR,
                      "天气服务请求失败");
        return;
    }
    json_end = json + rt_strlen(json);
    if (!json_find_member(json, json_end, "current", &current, &current_end) ||
        !json_find_member(json, json_end, "daily", &daily, &daily_end))
    {
        rt_free(json);
        rw_post_error(backend, cmd, LVWW_OP_WEATHER_FETCH, -RT_ERROR,
                      "天气数据格式错误");
        return;
    }
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WEATHER_RESULT;
    if (!json_member_number(current, current_end, "temperature_2m", &value)) goto bad;
    event.data.weather.temperature_c = (float)value;
    if (!json_member_number(current, current_end, "apparent_temperature", &value)) goto bad;
    event.data.weather.apparent_c = (float)value;
    if (!json_member_number(current, current_end, "relative_humidity_2m", &value)) goto bad;
    event.data.weather.humidity_percent = (float)value;
    if (!json_member_number(current, current_end, "weather_code", &value)) goto bad;
    event.data.weather.code = map_weather_code((int)value);
    if (!json_member_number(current, current_end, "wind_speed_10m", &value)) goto bad;
    event.data.weather.wind_kph = (float)value;
    if (!json_member_number(current, current_end, "time", &value)) goto bad;
    event.data.weather.observed_utc = (uint64_t)value;
    if (!json_member_array_number(daily, daily_end, "temperature_2m_max", 0, &value)) goto bad;
    event.data.weather.today_high_c = (float)value;
    if (!json_member_array_number(daily, daily_end, "temperature_2m_min", 0, &value)) goto bad;
    event.data.weather.today_low_c = (float)value;
    if (json_member_number(json, json_end, "utc_offset_seconds", &value))
        event.data.weather.utc_offset_seconds = (int32_t)value;
    rt_free(json);
    rw_post(backend, cmd, &event);
    return;
bad:
    rt_free(json);
    rw_post_error(backend, cmd, LVWW_OP_WEATHER_FETCH, -RT_ERROR,
                  "天气数据字段缺失");
}

static void rw_do_time(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    static const char path[] =
        "/v1/forecast?latitude=39.9042&longitude=116.4074&current=temperature_2m";
    char *json = RT_NULL;
    uint64_t server_time = 0;
    lvww_event_t event;
    if (!rt_wlan_is_ready() ||
        http_get_json(LVWW_FORECAST_HOST, path, &json, &server_time) != RT_EOK ||
        server_time < 1577836800u)
    {
        if (json) rt_free(json);
        rw_post_error(backend, cmd, LVWW_OP_TIME_SYNC, -RT_ERROR,
                      "网络校时失败");
        return;
    }
    rt_free(json);
    set_timestamp((time_t)server_time);
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_TIME_RESULT;
    event.data.time.utc_epoch = server_time;
    rw_post(backend, cmd, &event);
}

static void rw_worker_entry(void *parameter)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)parameter;
    lvww_rw_cmd_t cmd;
    while (backend->running)
    {
        if (rt_mq_recv(backend->queue, &cmd, sizeof(cmd),
                       RT_WAITING_FOREVER) != RT_EOK)
            continue;
        if (cmd.type == RW_CMD_STOP)
            break;
        if (!rw_request_current(backend, rw_operation(cmd.type), cmd.request_id))
            continue;
        switch (cmd.type)
        {
        case RW_CMD_SCAN: rw_do_scan(backend, &cmd); break;
        case RW_CMD_CONNECT: rw_do_connect(backend, &cmd); break;
        case RW_CMD_DISCONNECT: rw_do_disconnect(backend, &cmd); break;
        case RW_CMD_CITY: rw_do_city(backend, &cmd); break;
        case RW_CMD_WEATHER: rw_do_weather(backend, &cmd); break;
        case RW_CMD_TIME: rw_do_time(backend, &cmd); break;
        default: break;
        }
        rt_memset(&cmd, 0, sizeof(cmd));
    }
    backend->running = RT_FALSE;
    rt_sem_release(backend->stopped);
}

static int rw_send(lvww_rw007_t *backend, const lvww_rw_cmd_t *cmd)
{
    lvww_operation_t operation;
    if (!backend || !cmd || !backend->running) return -RT_ERROR;
    operation = rw_operation(cmd->type);
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->latest[operation] = cmd->request_id;
    backend->cancelled[operation] = 0;
    rt_mutex_release(backend->lock);
    return rt_mq_send(backend->queue, cmd, sizeof(*cmd));
}

static int rw_wifi_scan(void *user_ctx, uint32_t request_id)
{
    lvww_rw_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_SCAN;
    cmd.request_id = request_id;
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static int rw_wifi_connect(void *user_ctx, uint32_t request_id,
                           const lvww_wifi_credentials_t *credentials)
{
    lvww_rw_cmd_t cmd;
    if (!credentials) return -RT_EINVAL;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_CONNECT;
    cmd.request_id = request_id;
    cmd.data.credentials = *credentials;
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static int rw_wifi_disconnect(void *user_ctx, uint32_t request_id)
{
    lvww_rw_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_DISCONNECT;
    cmd.request_id = request_id;
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static int rw_city_search(void *user_ctx, uint32_t request_id, const char *query)
{
    lvww_rw_cmd_t cmd;
    if (!query) return -RT_EINVAL;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_CITY;
    cmd.request_id = request_id;
    rt_strncpy(cmd.data.query, query, sizeof(cmd.data.query) - 1u);
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static int rw_weather_fetch(void *user_ctx, uint32_t request_id,
                            const lvww_city_t *city)
{
    lvww_rw_cmd_t cmd;
    if (!city) return -RT_EINVAL;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_WEATHER;
    cmd.request_id = request_id;
    cmd.data.city = *city;
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static int rw_time_sync(void *user_ctx, uint32_t request_id)
{
    lvww_rw_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = RW_CMD_TIME;
    cmd.request_id = request_id;
    return rw_send((lvww_rw007_t *)user_ctx, &cmd);
}

static void rw_cancel(void *user_ctx, lvww_operation_t operation,
                      uint32_t request_id)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)user_ctx;
    if (!backend || operation > LVWW_OP_STORAGE) return;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->cancelled[operation] = request_id;
    rt_mutex_release(backend->lock);
}

static int rw_kv_find(lvww_rw007_t *backend, const char *key)
{
    int i;
    for (i = 0; i < LVWW_RW007_KV_SLOTS; ++i)
        if (backend->kv[i].used && rt_strcmp(backend->kv[i].key, key) == 0)
            return i;
    return -1;
}

static int rw_kv_read(void *user_ctx, const char *key, void *buffer,
                      rt_size_t *length)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)user_ctx;
    int index;
    if (!backend || !key || !buffer || !length) return -RT_EINVAL;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    index = rw_kv_find(backend, key);
    if (index < 0)
    {
        rt_mutex_release(backend->lock);
        return -RT_ENOSYS;
    }
    if (*length < backend->kv[index].length)
    {
        *length = backend->kv[index].length;
        rt_mutex_release(backend->lock);
        return -RT_EFULL;
    }
    rt_memcpy(buffer, backend->kv[index].data, backend->kv[index].length);
    *length = backend->kv[index].length;
    rt_mutex_release(backend->lock);
    return RT_EOK;
}

static int rw_kv_write(void *user_ctx, const char *key, const void *data,
                       rt_size_t length, uint32_t flags)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)user_ctx;
    int index, i;
    (void)flags;
    if (!backend || !key || !data || length > LVWW_RW007_KV_SIZE)
        return -RT_EINVAL;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    index = rw_kv_find(backend, key);
    if (index < 0)
        for (i = 0; i < LVWW_RW007_KV_SLOTS; ++i)
            if (!backend->kv[i].used)
            {
                index = i;
                break;
            }
    if (index < 0)
    {
        rt_mutex_release(backend->lock);
        return -RT_EFULL;
    }
    rt_strncpy(backend->kv[index].key, key,
               sizeof(backend->kv[index].key) - 1u);
    rt_memcpy(backend->kv[index].data, data, length);
    backend->kv[index].length = length;
    backend->kv[index].used = RT_TRUE;
    rt_mutex_release(backend->lock);
    return RT_EOK;
}

static int rw_kv_delete(void *user_ctx, const char *key)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)user_ctx;
    int index;
    if (!backend || !key) return -RT_EINVAL;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    index = rw_kv_find(backend, key);
    if (index >= 0) rt_memset(&backend->kv[index], 0, sizeof(backend->kv[index]));
    rt_mutex_release(backend->lock);
    return index >= 0 ? RT_EOK : -RT_ENOSYS;
}

static const lvww_port_ops_t rw_ops = {
    rw_wifi_scan,
    rw_wifi_connect,
    rw_wifi_disconnect,
    rw_city_search,
    rw_weather_fetch,
    rw_time_sync,
    rw_cancel,
    rw_kv_read,
    rw_kv_write,
    rw_kv_delete
};

lvww_rw007_t *lvww_rw007_create(void)
{
    lvww_rw007_t *backend = (lvww_rw007_t *)rt_calloc(1, sizeof(*backend));
    if (!backend) return RT_NULL;
    backend->queue = rt_mq_create("lvwrq", sizeof(lvww_rw_cmd_t),
                                  LVWW_RW007_QUEUE_DEPTH, RT_IPC_FLAG_FIFO);
    backend->stopped = rt_sem_create("lvwrs", 0, RT_IPC_FLAG_FIFO);
    backend->scan_done = rt_sem_create("lvwsc", 0, RT_IPC_FLAG_FIFO);
    backend->lock = rt_mutex_create("lvwrl", RT_IPC_FLAG_FIFO);
    if (!backend->queue || !backend->stopped || !backend->scan_done ||
        !backend->lock)
    {
        if (backend->queue) rt_mq_delete(backend->queue);
        if (backend->stopped) rt_sem_delete(backend->stopped);
        if (backend->scan_done) rt_sem_delete(backend->scan_done);
        if (backend->lock) rt_mutex_delete(backend->lock);
        rt_free(backend);
        return RT_NULL;
    }
    backend->running = RT_TRUE;
    backend->worker = rt_thread_create("lvwwrw", rw_worker_entry, backend,
                                       LVWW_RW007_STACK_SIZE, 20, 10);
    if (!backend->worker)
    {
        rt_mq_delete(backend->queue);
        rt_sem_delete(backend->stopped);
        rt_sem_delete(backend->scan_done);
        rt_mutex_delete(backend->lock);
        rt_free(backend);
        return RT_NULL;
    }
    rt_wlan_register_event_handler(RT_WLAN_EVT_STA_DISCONNECTED,
                                   rw_disconnected_cb, backend);
    rt_thread_startup(backend->worker);
    return backend;
}

void lvww_rw007_bind(lvww_rw007_t *backend, lvww_ctx_t *ctx)
{
    lvww_event_t event;
    struct rt_wlan_info info;
    if (!backend) return;
    rt_mutex_take(backend->lock, RT_WAITING_FOREVER);
    backend->ctx = ctx;
    rt_mutex_release(backend->lock);
    if (!ctx || !rt_wlan_is_ready()) return;
    rt_memset(&event, 0, sizeof(event));
    rt_memset(&info, 0, sizeof(info));
    event.type = LVWW_EVT_WIFI_STATE;
    event.data.wifi_state.state = LVWW_WIFI_ONLINE;
    if (rt_wlan_get_info(&info) == RT_EOK)
    {
        rt_size_t length = info.ssid.len;
        if (length > LVWW_SSID_MAX_LEN) length = LVWW_SSID_MAX_LEN;
        rt_memcpy(event.data.wifi_state.ssid, info.ssid.val, length);
        event.data.wifi_state.ssid[length] = '\0';
    }
    lvww_post_event(ctx, &event);
}

const lvww_port_ops_t *lvww_rw007_get_ops(void)
{
    return &rw_ops;
}

void lvww_rw007_destroy(lvww_rw007_t *backend)
{
    lvww_rw_cmd_t cmd;
    if (!backend) return;
    lvww_rw007_bind(backend, RT_NULL);
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_STA_DISCONNECTED);
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_REPORT);
    rt_wlan_unregister_event_handler(RT_WLAN_EVT_SCAN_DONE);
    if (backend->running)
    {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = RW_CMD_STOP;
        rt_mq_send(backend->queue, &cmd, sizeof(cmd));
        if (rt_sem_take(backend->stopped,
                        rt_tick_from_millisecond(10000)) != RT_EOK)
            rt_thread_delete(backend->worker);
    }
    rt_mq_delete(backend->queue);
    rt_sem_delete(backend->stopped);
    rt_sem_delete(backend->scan_done);
    rt_mutex_delete(backend->lock);
    rt_memset(backend, 0, sizeof(*backend));
    rt_free(backend);
}
