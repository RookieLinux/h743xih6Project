#include "lvww_mock.h"

#include <string.h>
#include <time.h>

#ifndef LVWW_MOCK_STACK_SIZE
#define LVWW_MOCK_STACK_SIZE 4096
#endif

#define LVWW_MOCK_QUEUE_DEPTH 10
#define LVWW_MOCK_KV_SLOTS    3
#define LVWW_MOCK_KV_SIZE     1536

typedef struct
{
    char key[24];
    uint8_t data[LVWW_MOCK_KV_SIZE];
    rt_size_t length;
    rt_bool_t used;
} lvww_mock_kv_t;

typedef enum
{
    MOCK_CMD_SCAN = 0,
    MOCK_CMD_CONNECT,
    MOCK_CMD_DISCONNECT,
    MOCK_CMD_CITY,
    MOCK_CMD_WEATHER,
    MOCK_CMD_TIME,
    MOCK_CMD_STOP
} lvww_mock_cmd_type_t;

typedef struct
{
    lvww_mock_cmd_type_t type;
    uint32_t request_id;
    union
    {
        lvww_wifi_credentials_t credentials;
        lvww_city_t city;
        char query[48];
    } data;
} lvww_mock_cmd_t;

struct lvww_mock
{
    lvww_ctx_t *ctx;
    rt_mq_t queue;
    rt_thread_t worker;
    rt_sem_t stopped;
    rt_mutex_t lock;
    rt_bool_t running;
    rt_bool_t network_available;
    rt_bool_t storage_failure;
    uint32_t cancelled[LVWW_OP_STORAGE + 1];
    lvww_mock_kv_t kv[LVWW_MOCK_KV_SLOTS];
};

static lvww_operation_t mock_operation(lvww_mock_cmd_type_t type)
{
    switch (type)
    {
    case MOCK_CMD_SCAN: return LVWW_OP_WIFI_SCAN;
    case MOCK_CMD_CONNECT: return LVWW_OP_WIFI_CONNECT;
    case MOCK_CMD_DISCONNECT: return LVWW_OP_WIFI_DISCONNECT;
    case MOCK_CMD_CITY: return LVWW_OP_CITY_SEARCH;
    case MOCK_CMD_WEATHER: return LVWW_OP_WEATHER_FETCH;
    case MOCK_CMD_TIME: return LVWW_OP_TIME_SYNC;
    default: return LVWW_OP_NONE;
    }
}

static rt_bool_t mock_is_cancelled(lvww_mock_t *mock, lvww_operation_t op,
                                   uint32_t request_id)
{
    rt_bool_t cancelled;
    rt_mutex_take(mock->lock, RT_WAITING_FOREVER);
    cancelled = mock->cancelled[op] == request_id;
    rt_mutex_release(mock->lock);
    return cancelled;
}

static lvww_ctx_t *mock_bound_ctx(lvww_mock_t *mock)
{
    lvww_ctx_t *ctx;
    rt_mutex_take(mock->lock, RT_WAITING_FOREVER);
    ctx = mock->ctx;
    rt_mutex_release(mock->lock);
    return ctx;
}

static void mock_post(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd,
                      lvww_event_t *event)
{
    lvww_ctx_t *ctx = mock_bound_ctx(mock);
    if (!ctx || mock_is_cancelled(mock, mock_operation(cmd->type), cmd->request_id))
        return;
    event->request_id = cmd->request_id;
    lvww_post_event(ctx, event);
}

static void mock_post_error(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd,
                            lvww_operation_t op, int code, const char *text)
{
    lvww_event_t event;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_ERROR;
    event.data.error.operation = op;
    event.data.error.code = code;
    rt_strncpy(event.data.error.text, text, LVWW_ERROR_TEXT_MAX_LEN);
    mock_post(mock, cmd, &event);
}

static void mock_scan(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    static const lvww_wifi_ap_t aps[] = {
        {"Home-2.4G", -38, 1},
        {"Office-5G", -54, 1},
        {"Coffee-Free", -63, 0},
        {"IoT-Lab", -71, 1},
        {"Guest", -78, 1}
    };
    lvww_event_t event;
    rt_thread_mdelay(450);
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_SCAN_RESULT;
    event.data.wifi_scan.count = sizeof(aps) / sizeof(aps[0]);
    rt_memcpy(event.data.wifi_scan.items, aps, sizeof(aps));
    mock_post(mock, cmd, &event);
}

static void mock_connect(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    lvww_event_t event;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    event.data.wifi_state.state = LVWW_WIFI_CONNECTING;
    rt_strncpy(event.data.wifi_state.ssid, cmd->data.credentials.ssid, LVWW_SSID_MAX_LEN);
    mock_post(mock, cmd, &event);
    rt_thread_mdelay(800);

    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    rt_strncpy(event.data.wifi_state.ssid, cmd->data.credentials.ssid, LVWW_SSID_MAX_LEN);
    if (!mock->network_available)
    {
        event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
        event.data.wifi_state.reason = LVWW_WIFI_REASON_TIMEOUT;
    }
    else if (cmd->data.credentials.secure &&
             strcmp(cmd->data.credentials.password, "wrongpass") == 0)
    {
        event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
        event.data.wifi_state.reason = LVWW_WIFI_REASON_AUTH;
    }
    else
    {
        event.data.wifi_state.state = LVWW_WIFI_ONLINE;
        event.data.wifi_state.reason = LVWW_WIFI_REASON_NONE;
    }
    mock_post(mock, cmd, &event);
}

static void mock_disconnect(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    lvww_event_t event;
    rt_thread_mdelay(300);
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WIFI_STATE;
    event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
    event.data.wifi_state.reason = LVWW_WIFI_REASON_NONE;
    mock_post(mock, cmd, &event);
}

static void mock_city(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    lvww_event_t event;
    rt_thread_mdelay(500);
    if (!mock->network_available)
    {
        mock_post_error(mock, cmd, LVWW_OP_CITY_SEARCH, -RT_ETIMEOUT, "城市服务不可用");
        return;
    }
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_CITY_SEARCH_RESULT;
    event.data.city_search.count = (uint16_t)lvww_city_catalog_search(
        cmd->data.query, event.data.city_search.items, LVWW_MAX_CITY_RESULTS);
    mock_post(mock, cmd, &event);
}

static int32_t mock_offset_for_city(const lvww_city_t *city)
{
    if (strcmp(city->timezone, "Europe/London") == 0)
        return 3600;
    if (strcmp(city->timezone, "America/New_York") == 0)
        return -4 * 3600;
    return 8 * 3600;
}

static void mock_weather(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    lvww_event_t event;
    unsigned seed = (unsigned)(cmd->data.city.latitude < 0 ?
                               -cmd->data.city.latitude : cmd->data.city.latitude);
    rt_thread_mdelay(650);
    if (!mock->network_available)
    {
        mock_post_error(mock, cmd, LVWW_OP_WEATHER_FETCH, -RT_ETIMEOUT, "天气服务请求失败");
        return;
    }
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_WEATHER_RESULT;
    event.data.weather.code = (lvww_weather_code_t)(seed % 4);
    event.data.weather.temperature_c = 21.5f + (float)(seed % 9);
    event.data.weather.apparent_c = event.data.weather.temperature_c + 0.8f;
    event.data.weather.humidity_percent = 48.0f + (float)(seed % 30);
    event.data.weather.wind_kph = 8.0f + (float)(seed % 11);
    event.data.weather.today_high_c = event.data.weather.temperature_c + 3.0f;
    event.data.weather.today_low_c = event.data.weather.temperature_c - 6.0f;
    event.data.weather.utc_offset_seconds = mock_offset_for_city(&cmd->data.city);
    event.data.weather.observed_utc = (uint64_t)time(RT_NULL);
    mock_post(mock, cmd, &event);
}

static void mock_time(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    lvww_event_t event;
    time_t now;
    rt_thread_mdelay(350);
    if (!mock->network_available)
    {
        mock_post_error(mock, cmd, LVWW_OP_TIME_SYNC, -RT_ETIMEOUT, "网络校时失败");
        return;
    }
    now = time(RT_NULL);
    if (now < 1577836800)
        now = (time_t)(1784073600u + rt_tick_get() / RT_TICK_PER_SECOND);
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_TIME_RESULT;
    event.data.time.utc_epoch = (uint64_t)now;
    mock_post(mock, cmd, &event);
}

static void mock_worker_entry(void *parameter)
{
    lvww_mock_t *mock = (lvww_mock_t *)parameter;
    lvww_mock_cmd_t cmd;
    while (mock->running)
    {
        if (rt_mq_recv(mock->queue, &cmd, sizeof(cmd), RT_WAITING_FOREVER) != RT_EOK)
            continue;
        if (cmd.type == MOCK_CMD_STOP)
            break;
        switch (cmd.type)
        {
        case MOCK_CMD_SCAN: mock_scan(mock, &cmd); break;
        case MOCK_CMD_CONNECT: mock_connect(mock, &cmd); break;
        case MOCK_CMD_DISCONNECT: mock_disconnect(mock, &cmd); break;
        case MOCK_CMD_CITY: mock_city(mock, &cmd); break;
        case MOCK_CMD_WEATHER: mock_weather(mock, &cmd); break;
        case MOCK_CMD_TIME: mock_time(mock, &cmd); break;
        default: break;
        }
        rt_memset(&cmd, 0, sizeof(cmd));
    }
    mock->running = RT_FALSE;
    rt_sem_release(mock->stopped);
}

static int mock_send(lvww_mock_t *mock, const lvww_mock_cmd_t *cmd)
{
    if (!mock || !mock->running)
        return -RT_ERROR;
    return rt_mq_send(mock->queue, cmd, sizeof(*cmd));
}

static int mock_wifi_scan(void *user_ctx, uint32_t request_id)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_SCAN;
    cmd.request_id = request_id;
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static int mock_wifi_connect(void *user_ctx, uint32_t request_id,
                             const lvww_wifi_credentials_t *credentials)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_CONNECT;
    cmd.request_id = request_id;
    cmd.data.credentials = *credentials;
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static int mock_wifi_disconnect(void *user_ctx, uint32_t request_id)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_DISCONNECT;
    cmd.request_id = request_id;
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static int mock_city_search(void *user_ctx, uint32_t request_id, const char *query)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_CITY;
    cmd.request_id = request_id;
    rt_strncpy(cmd.data.query, query, sizeof(cmd.data.query) - 1);
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static int mock_weather_fetch(void *user_ctx, uint32_t request_id,
                              const lvww_city_t *city)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_WEATHER;
    cmd.request_id = request_id;
    cmd.data.city = *city;
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static int mock_time_sync(void *user_ctx, uint32_t request_id)
{
    lvww_mock_cmd_t cmd;
    rt_memset(&cmd, 0, sizeof(cmd));
    cmd.type = MOCK_CMD_TIME;
    cmd.request_id = request_id;
    return mock_send((lvww_mock_t *)user_ctx, &cmd);
}

static void mock_cancel(void *user_ctx, lvww_operation_t operation, uint32_t request_id)
{
    lvww_mock_t *mock = (lvww_mock_t *)user_ctx;
    if (!mock || operation > LVWW_OP_STORAGE)
        return;
    rt_mutex_take(mock->lock, RT_WAITING_FOREVER);
    mock->cancelled[operation] = request_id;
    rt_mutex_release(mock->lock);
}

static int mock_kv_find(lvww_mock_t *mock, const char *key)
{
    int i;
    for (i = 0; i < LVWW_MOCK_KV_SLOTS; ++i)
        if (mock->kv[i].used && strcmp(mock->kv[i].key, key) == 0)
            return i;
    return -1;
}

static int mock_kv_read(void *user_ctx, const char *key, void *buffer, rt_size_t *length)
{
    lvww_mock_t *mock = (lvww_mock_t *)user_ctx;
    int index;
    if (!mock || !key || !buffer || !length || mock->storage_failure)
        return -RT_ERROR;
    index = mock_kv_find(mock, key);
    if (index < 0)
        return -RT_ENOSYS;
    if (*length < mock->kv[index].length)
    {
        *length = mock->kv[index].length;
        return -RT_EFULL;
    }
    rt_memcpy(buffer, mock->kv[index].data, mock->kv[index].length);
    *length = mock->kv[index].length;
    return RT_EOK;
}

static int mock_kv_write(void *user_ctx, const char *key, const void *data,
                         rt_size_t length, uint32_t flags)
{
    lvww_mock_t *mock = (lvww_mock_t *)user_ctx;
    int index, i;
    (void)flags;
    if (!mock || !key || !data || length > LVWW_MOCK_KV_SIZE || mock->storage_failure)
        return -RT_ERROR;
    index = mock_kv_find(mock, key);
    if (index < 0)
    {
        for (i = 0; i < LVWW_MOCK_KV_SLOTS; ++i)
            if (!mock->kv[i].used)
            {
                index = i;
                break;
            }
    }
    if (index < 0)
        return -RT_EFULL;
    rt_strncpy(mock->kv[index].key, key, sizeof(mock->kv[index].key) - 1);
    rt_memcpy(mock->kv[index].data, data, length);
    mock->kv[index].length = length;
    mock->kv[index].used = RT_TRUE;
    return RT_EOK;
}

static int mock_kv_delete(void *user_ctx, const char *key)
{
    lvww_mock_t *mock = (lvww_mock_t *)user_ctx;
    int index = mock_kv_find(mock, key);
    if (index < 0)
        return -RT_ENOSYS;
    rt_memset(&mock->kv[index], 0, sizeof(mock->kv[index]));
    return RT_EOK;
}

static const lvww_port_ops_t mock_ops = {
    mock_wifi_scan,
    mock_wifi_connect,
    mock_wifi_disconnect,
    mock_city_search,
    mock_weather_fetch,
    mock_time_sync,
    mock_cancel,
    mock_kv_read,
    mock_kv_write,
    mock_kv_delete
};

lvww_mock_t *lvww_mock_create(void)
{
    lvww_mock_t *mock = (lvww_mock_t *)rt_calloc(1, sizeof(*mock));
    if (!mock)
        return RT_NULL;
    mock->network_available = RT_TRUE;
    mock->queue = rt_mq_create("lvwmq", sizeof(lvww_mock_cmd_t), LVWW_MOCK_QUEUE_DEPTH,
                               RT_IPC_FLAG_FIFO);
    mock->stopped = rt_sem_create("lvwst", 0, RT_IPC_FLAG_FIFO);
    mock->lock = rt_mutex_create("lvwml", RT_IPC_FLAG_FIFO);
    if (!mock->queue || !mock->stopped || !mock->lock)
    {
        if (mock->queue) rt_mq_delete(mock->queue);
        if (mock->stopped) rt_sem_delete(mock->stopped);
        if (mock->lock) rt_mutex_delete(mock->lock);
        rt_free(mock);
        return RT_NULL;
    }
    mock->running = RT_TRUE;
    mock->worker = rt_thread_create("lvwwmock", mock_worker_entry, mock,
                                    LVWW_MOCK_STACK_SIZE, 20, 10);
    if (!mock->worker)
    {
        rt_mq_delete(mock->queue);
        rt_sem_delete(mock->stopped);
        rt_mutex_delete(mock->lock);
        rt_free(mock);
        return RT_NULL;
    }
    rt_thread_startup(mock->worker);
    return mock;
}

void lvww_mock_bind(lvww_mock_t *mock, lvww_ctx_t *ctx)
{
    if (!mock)
        return;
    rt_mutex_take(mock->lock, RT_WAITING_FOREVER);
    mock->ctx = ctx;
    rt_mutex_release(mock->lock);
}

const lvww_port_ops_t *lvww_mock_get_ops(void)
{
    return &mock_ops;
}

void lvww_mock_set_network_available(lvww_mock_t *mock, rt_bool_t available)
{
    lvww_event_t event;
    lvww_ctx_t *ctx;
    if (!mock)
        return;
    mock->network_available = available;
    if (!available && (ctx = mock_bound_ctx(mock)) != RT_NULL)
    {
        rt_memset(&event, 0, sizeof(event));
        event.type = LVWW_EVT_WIFI_STATE;
        event.request_id = 0;
        event.data.wifi_state.state = LVWW_WIFI_OFFLINE;
        event.data.wifi_state.reason = LVWW_WIFI_REASON_DROPPED;
        lvww_post_event(ctx, &event);
    }
}

void lvww_mock_set_storage_failure(lvww_mock_t *mock, rt_bool_t fail)
{
    if (mock)
        mock->storage_failure = fail;
}

void lvww_mock_destroy(lvww_mock_t *mock)
{
    lvww_mock_cmd_t cmd;
    if (!mock)
        return;
    lvww_mock_bind(mock, RT_NULL);
    if (mock->running)
    {
        rt_memset(&cmd, 0, sizeof(cmd));
        cmd.type = MOCK_CMD_STOP;
        rt_mq_send(mock->queue, &cmd, sizeof(cmd));
        if (rt_sem_take(mock->stopped, rt_tick_from_millisecond(2500)) != RT_EOK)
            rt_thread_delete(mock->worker);
    }
    rt_mq_delete(mock->queue);
    rt_sem_delete(mock->stopped);
    rt_mutex_delete(mock->lock);
    rt_memset(mock, 0, sizeof(*mock));
    rt_free(mock);
}
