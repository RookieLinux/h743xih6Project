#include "lvww_private.h"
#include "lvww_assets.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

uint32_t lvww_next_request(lvww_ctx_t *ctx)
{
    ++ctx->next_request_id;
    if (ctx->next_request_id == 0)
        ++ctx->next_request_id;
    return ctx->next_request_id;
}

static rt_bool_t lvww_retry_due(rt_tick_t retry_tick)
{
    if (retry_tick == 0)
        return RT_TRUE;
    return (rt_int32_t)(rt_tick_get() - retry_tick) >= 0;
}

static rt_tick_t lvww_retry_deadline(void)
{
    rt_tick_t deadline = rt_tick_get() +
        (rt_tick_t)(LVWW_NETWORK_RETRY_SECONDS * RT_TICK_PER_SECOND);
    return deadline == 0 ? 1 : deadline;
}


void lvww_request_scan(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.wifi_scan || ctx->scan_request_id)
        return;
    ctx->scan_request_id = lvww_next_request(ctx);
    ctx->wifi_state = ctx->wifi_state == LVWW_WIFI_ONLINE ? LVWW_WIFI_ONLINE : LVWW_WIFI_SCANNING;
    lv_label_set_text(ctx->wifi_status, "正在扫描热点...");
    rc = ctx->ops.wifi_scan(ctx->user_ctx, ctx->scan_request_id);
    if (rc != RT_EOK)
    {
        ctx->scan_request_id = 0;
        ctx->wifi_state = LVWW_WIFI_OFFLINE;
        lvww_show_toast(ctx, "无法启动 Wi-Fi 扫描", RT_TRUE);
    }
}

void lvww_request_connect(lvww_ctx_t *ctx, const lvww_wifi_credentials_t *credentials)
{
    int rc;
    if (!ctx->ops.wifi_connect || !credentials || ctx->connect_request_id)
        return;
    ctx->pending_credentials = *credentials;
    ctx->pending_valid = RT_TRUE;
    ctx->connect_request_id = lvww_next_request(ctx);
    ctx->wifi_state = LVWW_WIFI_CONNECTING;
    lvww_refresh_home(ctx);
    lv_label_set_text_fmt(ctx->wifi_status, "正在连接 %s...", credentials->ssid);
    rc = ctx->ops.wifi_connect(ctx->user_ctx, ctx->connect_request_id, credentials);
    if (rc != RT_EOK)
    {
        ctx->connect_request_id = 0;
        ctx->wifi_state = LVWW_WIFI_OFFLINE;
        lvww_secure_zero(&ctx->pending_credentials, sizeof(ctx->pending_credentials));
        ctx->pending_valid = RT_FALSE;
        lvww_show_toast(ctx, "无法启动连接", RT_TRUE);
    }
}

void lvww_request_disconnect(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.wifi_disconnect || ctx->disconnect_request_id)
        return;
    ctx->disconnect_request_id = lvww_next_request(ctx);
    ctx->wifi_state = LVWW_WIFI_DISCONNECTING;
    rc = ctx->ops.wifi_disconnect(ctx->user_ctx, ctx->disconnect_request_id);
    if (rc != RT_EOK)
    {
        ctx->disconnect_request_id = 0;
        lvww_show_toast(ctx, "无法启动断开操作", RT_TRUE);
    }
}

void lvww_request_weather(lvww_ctx_t *ctx)
{
    int rc;
    if (ctx->wifi_state != LVWW_WIFI_ONLINE || !ctx->cache_store.city_valid ||
        !ctx->ops.weather_fetch || ctx->weather_request_id ||
        !lvww_retry_due(ctx->weather_retry_tick))
        return;
    ctx->weather_retry_tick = 0;
    ctx->weather_request_id = lvww_next_request(ctx);
    rc = ctx->ops.weather_fetch(ctx->user_ctx, ctx->weather_request_id,
                                &ctx->cache_store.city);
    if (rc != RT_EOK)
    {
        ctx->weather_request_id = 0;
        ctx->weather_retry_tick = lvww_retry_deadline();
        lvww_show_toast(ctx, "无法启动天气更新", RT_TRUE);
    }
}

void lvww_request_time(lvww_ctx_t *ctx)
{
    int rc;
    if (ctx->wifi_state != LVWW_WIFI_ONLINE || !ctx->ops.time_sync ||
        ctx->time_request_id || !lvww_retry_due(ctx->time_retry_tick))
        return;
    ctx->time_retry_tick = 0;
    ctx->time_request_id = lvww_next_request(ctx);
    rc = ctx->ops.time_sync(ctx->user_ctx, ctx->time_request_id);
    if (rc != RT_EOK)
    {
        ctx->time_request_id = 0;
        ctx->time_retry_tick = lvww_retry_deadline();
        lvww_show_toast(ctx, "无法启动网络校时", RT_TRUE);
    }
}


static void lvww_handle_wifi_state(lvww_ctx_t *ctx, const lvww_event_t *event)
{
    ctx->wifi_state = event->data.wifi_state.state;
    if (ctx->wifi_state == LVWW_WIFI_ONLINE)
    {
        lvww_copy_text(ctx->connected_ssid, sizeof(ctx->connected_ssid),
                       event->data.wifi_state.ssid);
        lvww_upsert_pending_profile(ctx);
        ctx->connect_request_id = 0;
        ctx->disconnect_request_id = 0;
        ctx->weather_retry_tick = 0;
        ctx->time_retry_tick = 0;
        lvww_show_toast(ctx, "Wi-Fi 已连接", RT_FALSE);
        lvww_request_time(ctx);
        lvww_request_weather(ctx);
    }
    else if (ctx->wifi_state == LVWW_WIFI_OFFLINE)
    {
        ctx->connected_ssid[0] = '\0';
        ctx->connect_request_id = 0;
        ctx->disconnect_request_id = 0;
        ctx->weather_retry_tick = 0;
        ctx->time_retry_tick = 0;
        if (ctx->pending_valid)
        {
            lvww_secure_zero(&ctx->pending_credentials, sizeof(ctx->pending_credentials));
            ctx->pending_valid = RT_FALSE;
        }
        if (event->data.wifi_state.reason == LVWW_WIFI_REASON_AUTH)
            lvww_show_toast(ctx, "Wi-Fi 密码错误", RT_TRUE);
        else if (event->data.wifi_state.reason != LVWW_WIFI_REASON_NONE)
            lvww_show_toast(ctx, "Wi-Fi 连接已断开", RT_TRUE);
    }
    lvww_refresh_home(ctx);
    lvww_refresh_wifi(ctx);
}

static rt_bool_t lvww_event_current(lvww_ctx_t *ctx, const lvww_event_t *event)
{
    if (event->request_id == 0)
        return RT_TRUE;
    switch (event->type)
    {
    case LVWW_EVT_WIFI_SCAN_RESULT: return event->request_id == ctx->scan_request_id;
    case LVWW_EVT_CITY_SEARCH_RESULT: return event->request_id == ctx->city_request_id;
    case LVWW_EVT_WEATHER_RESULT: return event->request_id == ctx->weather_request_id;
    case LVWW_EVT_TIME_RESULT: return event->request_id == ctx->time_request_id;
    case LVWW_EVT_FIRMWARE_INFO: return RT_TRUE;
    case LVWW_EVT_WIFI_STATE:
        return event->request_id == ctx->connect_request_id ||
               event->request_id == ctx->disconnect_request_id;
    case LVWW_EVT_ERROR:
        switch (event->data.error.operation)
        {
        case LVWW_OP_WIFI_SCAN: return event->request_id == ctx->scan_request_id;
        case LVWW_OP_WIFI_CONNECT: return event->request_id == ctx->connect_request_id;
        case LVWW_OP_WIFI_DISCONNECT: return event->request_id == ctx->disconnect_request_id;
        case LVWW_OP_CITY_SEARCH: return event->request_id == ctx->city_request_id;
        case LVWW_OP_WEATHER_FETCH: return event->request_id == ctx->weather_request_id;
        case LVWW_OP_TIME_SYNC: return event->request_id == ctx->time_request_id;
        default: return RT_TRUE;
        }
    default: return RT_TRUE;
    }
}

static void lvww_handle_error(lvww_ctx_t *ctx, const lvww_event_t *event)
{
    switch (event->data.error.operation)
    {
    case LVWW_OP_WIFI_SCAN: ctx->scan_request_id = 0; break;
    case LVWW_OP_WIFI_CONNECT: ctx->connect_request_id = 0; ctx->wifi_state = LVWW_WIFI_OFFLINE; break;
    case LVWW_OP_WIFI_DISCONNECT: ctx->disconnect_request_id = 0; break;
    case LVWW_OP_CITY_SEARCH: ctx->city_request_id = 0; break;
    case LVWW_OP_WEATHER_FETCH:
        ctx->weather_request_id = 0;
        ctx->weather_retry_tick = lvww_retry_deadline();
        break;
    case LVWW_OP_TIME_SYNC:
        ctx->time_request_id = 0;
        ctx->time_retry_tick = lvww_retry_deadline();
        break;
    default: break;
    }
    lvww_show_toast(ctx, event->data.error.text[0] ? event->data.error.text : "操作失败",
                    RT_TRUE);
    lvww_refresh_home(ctx);
    lvww_refresh_wifi(ctx);
}

static void lvww_handle_event(lvww_ctx_t *ctx, const lvww_event_t *event)
{
    uint16_t count;
    if (!lvww_event_current(ctx, event))
        return;
    switch (event->type)
    {
    case LVWW_EVT_WIFI_SCAN_RESULT:
        count = event->data.wifi_scan.count;
        if (count > ctx->cfg.max_wifi_results)
            count = ctx->cfg.max_wifi_results;
        rt_memcpy(ctx->wifi_results, event->data.wifi_scan.items,
                  count * sizeof(ctx->wifi_results[0]));
        ctx->wifi_count = count;
        ctx->scan_request_id = 0;
        if (ctx->wifi_state == LVWW_WIFI_SCANNING)
            ctx->wifi_state = LVWW_WIFI_OFFLINE;
        lvww_refresh_wifi(ctx);
        lvww_refresh_home(ctx);
        break;
    case LVWW_EVT_WIFI_STATE:
        lvww_handle_wifi_state(ctx, event);
        break;
    case LVWW_EVT_CITY_SEARCH_RESULT:
        count = event->data.city_search.count;
        if (count > ctx->cfg.max_city_results)
            count = ctx->cfg.max_city_results;
        rt_memcpy(ctx->city_candidates, event->data.city_search.items,
                  count * sizeof(ctx->city_candidates[0]));
        ctx->city_count = count;
        ctx->city_request_id = 0;
        lvww_refresh_city_results(ctx);
        break;
    case LVWW_EVT_WEATHER_RESULT:
        ctx->cache_store.weather = event->data.weather;
        ctx->cache_store.weather_valid = 1;
        ctx->weather_tick = rt_tick_get();
        ctx->weather_request_id = 0;
        ctx->weather_retry_tick = 0;
        lvww_save_cache(ctx);
        lvww_refresh_home(ctx);
        break;
    case LVWW_EVT_TIME_RESULT:
        ctx->utc_epoch = event->data.time.utc_epoch;
        ctx->utc_tick = rt_tick_get();
        ctx->time_tick = ctx->utc_tick;
        ctx->time_valid = RT_TRUE;
        ctx->time_request_id = 0;
        ctx->time_retry_tick = 0;
        lvww_refresh_clock(ctx);
        break;
    case LVWW_EVT_FIRMWARE_INFO:
        ctx->firmware_info = event->data.firmware;
        ctx->firmware_info.current_version[
            LVWW_FIRMWARE_VERSION_MAX_LEN] = '\0';
        ctx->firmware_info.available_version[
            LVWW_FIRMWARE_VERSION_MAX_LEN] = '\0';
        ctx->firmware_info.release_notes[
            LVWW_RELEASE_NOTES_MAX_LEN] = '\0';
        if (ctx->firmware_info.progress_percent > 100U)
            ctx->firmware_info.progress_percent = 100U;
        lvww_refresh_firmware(ctx);
        break;
    case LVWW_EVT_ERROR:
        lvww_handle_error(ctx, event);
        break;
    default:
        break;
    }
}

static void lvww_pump_timer_cb(lv_timer_t *timer)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)timer->user_data;
    int processed = 0;
    while (processed < 4 &&
           rt_mq_recv(ctx->event_mq, &ctx->pump_event,
                      sizeof(ctx->pump_event), 0) == RT_EOK)
    {
        lvww_handle_event(ctx, &ctx->pump_event);
        ++processed;
    }
}

static void lvww_clock_timer_cb(lv_timer_t *timer)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)timer->user_data;
    rt_tick_t now = rt_tick_get();
    lvww_refresh_clock(ctx);
    if (ctx->wifi_state != LVWW_WIFI_ONLINE)
        return;
    if (!ctx->time_valid ||
        (uint32_t)((now - ctx->time_tick) / RT_TICK_PER_SECOND) >= ctx->cfg.time_sync_seconds)
        lvww_request_time(ctx);
    if (!ctx->cache_store.weather_valid ||
        (uint32_t)((now - ctx->weather_tick) / RT_TICK_PER_SECOND) >=
            ctx->cfg.weather_refresh_seconds)
        lvww_request_weather(ctx);
}


void lvww_config_init(lvww_config_t *config)
{
    const lv_font_t *external_font;
    if (!config)
        return;
    rt_memset(config, 0, sizeof(*config));
    config->width = 800;
    config->height = 480;
    config->max_profiles = LVWW_MAX_PROFILES;
    config->max_wifi_results = LVWW_MAX_WIFI_RESULTS;
    config->max_city_results = LVWW_MAX_CITY_RESULTS;
    config->weather_refresh_seconds = 30u * 60u;
    config->time_sync_seconds = 6u * 60u * 60u;
    config->background_color = lv_color_hex(0x0D1422);
    config->panel_color = lv_color_hex(0x202B40);
    config->accent_color = lv_color_hex(0x2F80ED);
    config->font_ui = &lvww_font_cjk_16;
    lvww_assets_init();
    external_font = lvww_assets_font();
#if defined(LV_FONT_MONTSERRAT_48) && LV_FONT_MONTSERRAT_48
    config->font_large = &lv_font_montserrat_48;
#else
    config->font_large = config->font_ui;
#endif
    config->font_city = external_font ? external_font : config->font_ui;
    lvww_city_catalog_search("beijing", &config->default_city, 1);
}

void lvww_firmware_info_init(lvww_firmware_info_t *info)
{
    if (!info)
        return;
    rt_memset(info, 0, sizeof(*info));
    info->state = LVWW_FIRMWARE_CHECKING;
    rt_strncpy(info->current_version, "未知",
               sizeof(info->current_version) - 1U);
    rt_strncpy(info->available_version, "等待服务器",
               sizeof(info->available_version) - 1U);
    rt_strncpy(info->release_notes,
               "设备联网后将通过 MQTT 查询最新固件。",
               sizeof(info->release_notes) - 1U);
}

lvww_ctx_t *lvww_create(lv_obj_t *parent, const lvww_config_t *config,
                        const lvww_port_ops_t *port_ops, void *user_ctx)
{
    lvww_ctx_t *ctx;
    lvww_config_t defaults;
    char mq_name[RT_NAME_MAX];
    char lock_name[RT_NAME_MAX];

    if (!parent || !port_ops)
        return RT_NULL;
    ctx = (lvww_ctx_t *)rt_calloc(1, sizeof(*ctx));
    if (!ctx)
        return RT_NULL;
    lvww_config_init(&defaults);
    ctx->cfg = config ? *config : defaults;
    if (!ctx->cfg.width) ctx->cfg.width = defaults.width;
    if (!ctx->cfg.height) ctx->cfg.height = defaults.height;
    if (!ctx->cfg.font_ui) ctx->cfg.font_ui = defaults.font_ui;
    if (!ctx->cfg.font_large) ctx->cfg.font_large = ctx->cfg.font_ui;
    if (!ctx->cfg.font_city) ctx->cfg.font_city = ctx->cfg.font_ui;
    if (!ctx->cfg.max_profiles || ctx->cfg.max_profiles > LVWW_MAX_PROFILES)
        ctx->cfg.max_profiles = LVWW_MAX_PROFILES;
    if (!ctx->cfg.max_wifi_results || ctx->cfg.max_wifi_results > LVWW_MAX_WIFI_RESULTS)
        ctx->cfg.max_wifi_results = LVWW_MAX_WIFI_RESULTS;
    if (!ctx->cfg.max_city_results || ctx->cfg.max_city_results > LVWW_MAX_CITY_RESULTS)
        ctx->cfg.max_city_results = LVWW_MAX_CITY_RESULTS;
    if (!ctx->cfg.weather_refresh_seconds)
        ctx->cfg.weather_refresh_seconds = defaults.weather_refresh_seconds;
    if (!ctx->cfg.time_sync_seconds)
        ctx->cfg.time_sync_seconds = defaults.time_sync_seconds;

    ctx->ops = *port_ops;
    ctx->user_ctx = user_ctx;
    ctx->alive = RT_TRUE;
    ctx->editor_saved_index = LVWW_INVALID_INDEX;
    lvww_firmware_info_init(&ctx->firmware_info);
    rt_snprintf(mq_name, sizeof(mq_name), "lvw%02x", (unsigned)((uintptr_t)ctx & 0xFFu));
    rt_snprintf(lock_name, sizeof(lock_name), "lvl%02x", (unsigned)((uintptr_t)ctx & 0xFFu));
    ctx->event_mq = rt_mq_create(mq_name, sizeof(lvww_event_t), LVWW_EVENT_QUEUE_DEPTH,
                                 RT_IPC_FLAG_FIFO);
    ctx->lock = rt_mutex_create(lock_name, RT_IPC_FLAG_FIFO);
    if (!ctx->event_mq || !ctx->lock)
    {
        if (ctx->event_mq) rt_mq_delete(ctx->event_mq);
        if (ctx->lock) rt_mutex_delete(ctx->lock);
        rt_free(ctx);
        return RT_NULL;
    }

    lvww_load_store(ctx);
    lvww_build_ui(ctx, parent);
    ctx->pump_timer = lv_timer_create(lvww_pump_timer_cb, 40, ctx);
    ctx->clock_timer = lv_timer_create(lvww_clock_timer_cb, 1000, ctx);
    ctx->search_timer = lv_timer_create(lvww_search_timer_cb, 500, ctx);
    ctx->toast_timer = lv_timer_create(lvww_toast_hide_cb, 2600, ctx);
    lv_timer_pause(ctx->search_timer);
    lv_timer_pause(ctx->toast_timer);
    lvww_refresh_home(ctx);
    lvww_refresh_wifi(ctx);

    if (ctx->profile_store.last_success >= 0 &&
        ctx->profile_store.last_success < ctx->profile_store.count)
        lvww_request_connect(ctx,
                             &ctx->profile_store.profiles[ctx->profile_store.last_success]);
    else
        lvww_request_scan(ctx);
    return ctx;
}

int lvww_post_event(lvww_ctx_t *ctx, const lvww_event_t *event)
{
    int rc;
    if (!ctx || !event || !ctx->lock)
        return -RT_EINVAL;
    if (rt_mutex_take(ctx->lock, RT_WAITING_FOREVER) != RT_EOK)
        return -RT_ERROR;
    if (!ctx->alive || !ctx->event_mq)
    {
        rt_mutex_release(ctx->lock);
        return -RT_EINVAL;
    }
    rc = rt_mq_send(ctx->event_mq, event, sizeof(*event));
    rt_mutex_release(ctx->lock);
    return rc;
}

int lvww_set_firmware_info(lvww_ctx_t *ctx,
                           const lvww_firmware_info_t *info)
{
    lvww_event_t event;

    if (!ctx || !info)
        return -RT_EINVAL;
    rt_memset(&event, 0, sizeof(event));
    event.type = LVWW_EVT_FIRMWARE_INFO;
    event.data.firmware = *info;
    return lvww_post_event(ctx, &event);
}

void lvww_set_firmware_update_callback(
    lvww_ctx_t *ctx,
    lvww_firmware_update_cb_t callback,
    void *user_ctx)
{
    if (!ctx || !ctx->lock)
        return;
    if (rt_mutex_take(ctx->lock, RT_WAITING_FOREVER) != RT_EOK)
        return;
    if (ctx->alive)
    {
        ctx->firmware_update_cb = callback;
        ctx->firmware_update_user_ctx = user_ctx;
    }
    rt_mutex_release(ctx->lock);
}

void lvww_set_server_address(lvww_ctx_t *ctx, const char *ipv4_address)
{
    if (!ctx)
        return;
    lvww_copy_text(ctx->server_address, sizeof(ctx->server_address),
                   ipv4_address);
    if (ctx->home_server_input)
        lv_textarea_set_text(ctx->home_server_input, ctx->server_address);
}

void lvww_set_server_address_callback(
    lvww_ctx_t *ctx,
    lvww_server_address_cb_t callback,
    void *user_ctx)
{
    if (!ctx || !ctx->lock)
        return;
    if (rt_mutex_take(ctx->lock, RT_WAITING_FOREVER) != RT_EOK)
        return;
    if (ctx->alive)
    {
        ctx->server_address_cb = callback;
        ctx->server_address_user_ctx = user_ctx;
    }
    rt_mutex_release(ctx->lock);
}

void lvww_destroy(lvww_ctx_t *ctx)
{
    if (!ctx)
        return;
    rt_mutex_take(ctx->lock, RT_WAITING_FOREVER);
    ctx->alive = RT_FALSE;
    rt_mutex_release(ctx->lock);

    if (ctx->ops.cancel)
    {
        if (ctx->scan_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_WIFI_SCAN, ctx->scan_request_id);
        if (ctx->connect_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_WIFI_CONNECT, ctx->connect_request_id);
        if (ctx->disconnect_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_WIFI_DISCONNECT, ctx->disconnect_request_id);
        if (ctx->city_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_CITY_SEARCH, ctx->city_request_id);
        if (ctx->weather_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_WEATHER_FETCH, ctx->weather_request_id);
        if (ctx->time_request_id) ctx->ops.cancel(ctx->user_ctx, LVWW_OP_TIME_SYNC, ctx->time_request_id);
    }
    if (ctx->pump_timer) lv_timer_del(ctx->pump_timer);
    if (ctx->clock_timer) lv_timer_del(ctx->clock_timer);
    if (ctx->search_timer) lv_timer_del(ctx->search_timer);
    if (ctx->toast_timer) lv_timer_del(ctx->toast_timer);
    if (ctx->pinyin_ime)
    {
        /* The LVGL Pinyin IME owns both its keyboard and candidate panel. */
        lv_keyboard_set_textarea(ctx->keyboard, RT_NULL);
        lv_obj_del(ctx->pinyin_ime);
        ctx->pinyin_ime = RT_NULL;
        ctx->pinyin_candidates = RT_NULL;
        ctx->keyboard = RT_NULL;
    }
    else if (ctx->keyboard)
    {
        lv_keyboard_set_textarea(ctx->keyboard, RT_NULL);
        lv_obj_del(ctx->keyboard);
        ctx->keyboard = RT_NULL;
    }
    if (ctx->root) lv_obj_del(ctx->root);
    rt_mq_delete(ctx->event_mq);
    rt_mutex_delete(ctx->lock);
    lvww_secure_zero(&ctx->profile_store, sizeof(ctx->profile_store));
    lvww_secure_zero(&ctx->pending_credentials, sizeof(ctx->pending_credentials));
    rt_free(ctx);
}
