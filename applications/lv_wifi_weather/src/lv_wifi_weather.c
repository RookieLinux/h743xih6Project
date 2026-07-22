#include "lv_wifi_weather.h"
#include "lvww_assets.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#ifndef LVWW_EVENT_QUEUE_DEPTH
#define LVWW_EVENT_QUEUE_DEPTH 8
#endif

#define LVWW_PROFILE_KEY       "lvww/profiles"
#define LVWW_CACHE_KEY         "lvww/cache"
#define LVWW_STORE_MAGIC       0x4C565757u
#define LVWW_STORE_VERSION     1u
#define LVWW_INVALID_INDEX     (-1)
#define LVWW_WIFI_SAVED_TAG    0x100u

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t count;
    int8_t last_success;
    lvww_wifi_credentials_t profiles[LVWW_MAX_PROFILES];
    uint32_t crc;
} lvww_profile_store_t;

typedef struct
{
    uint32_t magic;
    uint16_t version;
    uint8_t city_valid;
    uint8_t weather_valid;
    lvww_city_t city;
    lvww_weather_t weather;
    uint32_t crc;
} lvww_cache_store_t;

typedef struct
{
    lvww_ctx_t *ctx;
    uintptr_t tag;
} lvww_binding_t;

struct lvww_ctx
{
    lvww_config_t cfg;
    lvww_port_ops_t ops;
    void *user_ctx;
    rt_mq_t event_mq;
    rt_mutex_t lock;
    rt_bool_t alive;

    lv_obj_t *root;
    lv_obj_t *pages[3];
    lv_obj_t *nav_buttons[3];
    lv_obj_t *home_city;
    lv_obj_t *home_clock;
    lv_obj_t *home_date;
    lv_obj_t *home_net;
    lv_obj_t *home_weather_icon;
    lv_obj_t *home_temperature;
    lv_obj_t *home_temperature_unit;
    lv_obj_t *home_apparent;
    lv_obj_t *home_humidity;
    lv_obj_t *home_wind;
    lv_obj_t *home_range;
    lv_obj_t *home_updated;
    lv_obj_t *wifi_status;
    lv_obj_t *wifi_list;
    lv_obj_t *city_input;
    lv_obj_t *city_results;
    lv_obj_t *keyboard;
    lv_obj_t *toast;
    lv_obj_t *editor;
    lv_obj_t *editor_ssid;
    lv_obj_t *editor_password;
    lv_obj_t *editor_security_label;
    lv_obj_t *editor_password_label;

    lvww_binding_t nav_bindings[3];
    lvww_binding_t wifi_bindings[LVWW_MAX_WIFI_RESULTS + LVWW_MAX_PROFILES];
    lvww_binding_t city_bindings[LVWW_MAX_CITY_RESULTS];
    uint8_t wifi_binding_count;

    lv_timer_t *pump_timer;
    lv_timer_t *clock_timer;
    lv_timer_t *search_timer;
    lv_timer_t *toast_timer;

    lvww_profile_store_t profile_store;
    lvww_cache_store_t cache_store;
    lvww_wifi_ap_t wifi_results[LVWW_MAX_WIFI_RESULTS];
    uint16_t wifi_count;
    lvww_city_t city_candidates[LVWW_MAX_CITY_RESULTS];
    uint16_t city_count;
    lvww_event_t pump_event;
    lvww_wifi_state_t wifi_state;
    char connected_ssid[LVWW_SSID_MAX_LEN + 1];

    uint64_t utc_epoch;
    rt_tick_t utc_tick;
    rt_tick_t weather_tick;
    rt_tick_t time_tick;
    rt_bool_t time_valid;

    uint32_t next_request_id;
    uint32_t scan_request_id;
    uint32_t connect_request_id;
    uint32_t disconnect_request_id;
    uint32_t city_request_id;
    uint32_t weather_request_id;
    uint32_t time_request_id;

    lvww_wifi_credentials_t pending_credentials;
    rt_bool_t pending_valid;
    int16_t editor_saved_index;
    rt_bool_t editor_secure;
};

enum
{
    LVWW_PAGE_HOME = 0,
    LVWW_PAGE_WIFI,
    LVWW_PAGE_CITY
};

typedef struct
{
    lvww_city_t city;
    const char *search_terms;
} lvww_city_catalog_item_t;

static const lvww_city_catalog_item_t lvww_city_catalog[] = {
    {{"beijing", "北京", "北京市", "中国", "Asia/Shanghai", 39.9042, 116.4074},
     "beijing 北京 北京市"},
    {{"shanghai", "上海", "上海市", "中国", "Asia/Shanghai", 31.2304, 121.4737},
     "shanghai 上海 上海市"},
    {{"shenzhen", "深圳", "广东省", "中国", "Asia/Shanghai", 22.5431, 114.0579},
     "shenzhen 深圳 广东 广东省"},
    {{"guangzhou", "广州", "广东省", "中国", "Asia/Shanghai", 23.1291, 113.2644},
     "guangzhou 广州 广东 广东省"},
    {{"chengdu", "成都", "四川省", "中国", "Asia/Shanghai", 30.5728, 104.0668},
     "chengdu 成都 四川 四川省"},
    {{"hangzhou", "杭州", "浙江省", "中国", "Asia/Shanghai", 30.2741, 120.1551},
     "hangzhou 杭州 浙江 浙江省"},
    {{"wuhan", "武汉", "湖北省", "中国", "Asia/Shanghai", 30.5928, 114.3055},
     "wuhan 武汉 湖北 湖北省"},
    {{"xian", "西安", "陕西省", "中国", "Asia/Shanghai", 34.3416, 108.9398},
     "xian xi'an 西安 陕西 陕西省"}
};

static void lvww_refresh_home(lvww_ctx_t *ctx);
static void lvww_refresh_wifi(lvww_ctx_t *ctx);
static void lvww_request_scan(lvww_ctx_t *ctx);
static void lvww_request_weather(lvww_ctx_t *ctx);
static void lvww_request_time(lvww_ctx_t *ctx);
static void lvww_close_editor(lvww_ctx_t *ctx);

static int lvww_ascii_lower(int c)
{
    return c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c;
}

static rt_bool_t lvww_text_contains_ci(const char *text, const char *query)
{
    rt_size_t text_len;
    rt_size_t query_len;
    rt_size_t i;
    rt_size_t j;

    if (!text || !query)
        return RT_FALSE;
    query_len = rt_strlen(query);
    if (!query_len)
        return RT_TRUE;
    text_len = rt_strlen(text);
    if (query_len > text_len)
        return RT_FALSE;
    for (i = 0; i + query_len <= text_len; ++i)
    {
        for (j = 0; j < query_len; ++j)
        {
            if (lvww_ascii_lower((unsigned char)text[i + j]) !=
                lvww_ascii_lower((unsigned char)query[j]))
                break;
        }
        if (j == query_len)
            return RT_TRUE;
    }
    return RT_FALSE;
}

rt_size_t lvww_city_catalog_search(const char *query,
                                   lvww_city_t *cities,
                                   rt_size_t capacity)
{
    rt_size_t i;
    rt_size_t count = 0;

    if (!cities || !capacity)
        return 0;
    if (!query)
        query = "";
    for (i = 0; i < sizeof(lvww_city_catalog) / sizeof(lvww_city_catalog[0]) &&
                count < capacity; ++i)
    {
        if (!query[0] ||
            lvww_text_contains_ci(lvww_city_catalog[i].search_terms, query))
            cities[count++] = lvww_city_catalog[i].city;
    }
    return count;
}

static void lvww_secure_zero(void *data, rt_size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (length--)
        *p++ = 0;
}

static uint32_t lvww_crc32(const void *data, rt_size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    rt_size_t i;
    int bit;

    for (i = 0; i < length; ++i)
    {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}

static uint32_t lvww_next_request(lvww_ctx_t *ctx)
{
    ++ctx->next_request_id;
    if (ctx->next_request_id == 0)
        ++ctx->next_request_id;
    return ctx->next_request_id;
}

static void lvww_copy_text(char *dst, rt_size_t dst_size, const char *src)
{
    if (!dst || dst_size == 0)
        return;
    if (!src)
        src = "";
    rt_strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static lv_obj_t *lvww_panel(lv_obj_t *parent, lv_color_t color, lv_coord_t radius)
{
    lv_obj_t *obj = lv_obj_create(parent);
    lv_obj_set_style_bg_color(obj, color, 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(obj, 0, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_set_style_pad_all(obj, 12, 0);
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    return obj;
}

static lv_obj_t *lvww_label(lvww_ctx_t *ctx, lv_obj_t *parent, const char *text,
                            const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text ? text : "");
    lv_obj_set_style_text_font(label, font ? font : ctx->cfg.font_ui, 0);
    lv_obj_set_style_text_color(label, color, 0);
    return label;
}

static lv_obj_t *lvww_button(lvww_ctx_t *ctx, lv_obj_t *parent, const char *text,
                             lv_coord_t width, lv_coord_t height)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, ctx->cfg.accent_color, 0);
    lv_obj_set_style_bg_color(button, lv_color_darken(ctx->cfg.accent_color, 40),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_t *label = lvww_label(ctx, button, text, ctx->cfg.font_ui, lv_color_white());
    lv_obj_center(label);
    return button;
}

static void lvww_style_textarea(lvww_ctx_t *ctx, lv_obj_t *textarea)
{
    const lv_style_selector_t focused = LV_PART_MAIN | LV_STATE_FOCUSED;
    const lv_style_selector_t cursor = LV_PART_CURSOR | LV_STATE_FOCUSED;

    lv_obj_set_style_border_width(textarea, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(textarea, lv_color_hex(0x53617A), LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_white(), LV_PART_MAIN);
    lv_obj_set_style_text_color(textarea, lv_color_white(), focused);
    lv_obj_set_style_border_width(textarea, 3, focused);
    lv_obj_set_style_border_color(textarea, ctx->cfg.accent_color, focused);
    lv_obj_set_style_outline_width(textarea, 2, focused);
    lv_obj_set_style_outline_pad(textarea, 2, focused);
    lv_obj_set_style_outline_color(textarea, ctx->cfg.accent_color, focused);
    lv_obj_set_style_outline_opa(textarea, LV_OPA_70, focused);
    lv_obj_set_style_bg_color(textarea, lv_color_hex(0x182F50), focused);
    lv_obj_set_style_bg_color(textarea, ctx->cfg.accent_color, cursor);
    lv_obj_set_style_bg_opa(textarea, LV_OPA_COVER, cursor);
}

static void lvww_toast_hide_cb(lv_timer_t *timer)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)timer->user_data;
    if (ctx && ctx->toast)
        lv_obj_add_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

static void lvww_show_toast(lvww_ctx_t *ctx, const char *text, rt_bool_t error)
{
    if (!ctx || !ctx->toast)
        return;
    lv_label_set_text(ctx->toast, text ? text : "");
    lv_obj_set_style_bg_color(ctx->toast,
                              error ? lv_color_hex(0xA53C4B) : lv_color_hex(0x247C68), 0);
    lv_obj_clear_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->toast);
    lv_timer_set_period(ctx->toast_timer, 2600);
    lv_timer_reset(ctx->toast_timer);
    lv_timer_resume(ctx->toast_timer);
}

static int lvww_find_profile(lvww_ctx_t *ctx, const char *ssid)
{
    uint8_t i;
    for (i = 0; i < ctx->profile_store.count; ++i)
    {
        if (rt_strncmp(ctx->profile_store.profiles[i].ssid, ssid,
                       LVWW_SSID_MAX_LEN + 1) == 0)
            return i;
    }
    return LVWW_INVALID_INDEX;
}

static void lvww_save_profiles(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.kv_write)
        return;
    ctx->profile_store.magic = LVWW_STORE_MAGIC;
    ctx->profile_store.version = LVWW_STORE_VERSION;
    ctx->profile_store.crc = lvww_crc32(&ctx->profile_store,
                                        offsetof(lvww_profile_store_t, crc));
    rc = ctx->ops.kv_write(ctx->user_ctx, LVWW_PROFILE_KEY, &ctx->profile_store,
                           sizeof(ctx->profile_store), LVWW_KV_FLAG_SECRET);
    if (rc != RT_EOK)
        lvww_show_toast(ctx, "Wi-Fi 配置保存失败", RT_TRUE);
}

static void lvww_save_cache(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.kv_write)
        return;
    ctx->cache_store.magic = LVWW_STORE_MAGIC;
    ctx->cache_store.version = LVWW_STORE_VERSION;
    ctx->cache_store.crc = lvww_crc32(&ctx->cache_store,
                                      offsetof(lvww_cache_store_t, crc));
    rc = ctx->ops.kv_write(ctx->user_ctx, LVWW_CACHE_KEY, &ctx->cache_store,
                           sizeof(ctx->cache_store), 0);
    if (rc != RT_EOK)
        lvww_show_toast(ctx, "天气设置保存失败", RT_TRUE);
}

static void lvww_load_store(lvww_ctx_t *ctx)
{
    rt_size_t length;
    uint32_t crc;
    lvww_city_t normalized_city;

    rt_memset(&ctx->profile_store, 0, sizeof(ctx->profile_store));
    ctx->profile_store.last_success = LVWW_INVALID_INDEX;
    if (ctx->ops.kv_read)
    {
        length = sizeof(ctx->profile_store);
        if (ctx->ops.kv_read(ctx->user_ctx, LVWW_PROFILE_KEY,
                             &ctx->profile_store, &length) != RT_EOK ||
            length != sizeof(ctx->profile_store))
        {
            rt_memset(&ctx->profile_store, 0, sizeof(ctx->profile_store));
            ctx->profile_store.last_success = LVWW_INVALID_INDEX;
        }
    }
    crc = lvww_crc32(&ctx->profile_store, offsetof(lvww_profile_store_t, crc));
    if (ctx->profile_store.magic != LVWW_STORE_MAGIC ||
        ctx->profile_store.version != LVWW_STORE_VERSION ||
        ctx->profile_store.count > ctx->cfg.max_profiles ||
        ctx->profile_store.crc != crc)
    {
        lvww_secure_zero(&ctx->profile_store, sizeof(ctx->profile_store));
        ctx->profile_store.last_success = LVWW_INVALID_INDEX;
    }

    rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
    if (ctx->ops.kv_read)
    {
        length = sizeof(ctx->cache_store);
        if (ctx->ops.kv_read(ctx->user_ctx, LVWW_CACHE_KEY,
                             &ctx->cache_store, &length) != RT_EOK ||
            length != sizeof(ctx->cache_store))
            rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
    }
    crc = lvww_crc32(&ctx->cache_store, offsetof(lvww_cache_store_t, crc));
    if (ctx->cache_store.magic != LVWW_STORE_MAGIC ||
        ctx->cache_store.version != LVWW_STORE_VERSION ||
        ctx->cache_store.crc != crc)
    {
        rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
        ctx->cache_store.city = ctx->cfg.default_city;
        ctx->cache_store.city_valid = ctx->cfg.default_city.id[0] != '\0';
    }
    else if (ctx->cache_store.city_valid &&
             lvww_city_catalog_search(ctx->cache_store.city.id,
                                      &normalized_city, 1) == 1 &&
             rt_strcmp(normalized_city.id, ctx->cache_store.city.id) == 0)
    {
        /* Migrate old English display names without discarding weather data. */
        ctx->cache_store.city = normalized_city;
    }
}

static const char *lvww_weather_icon(lvww_weather_code_t code)
{
    switch (code)
    {
    case LVWW_WEATHER_CLEAR: return "晴";
    case LVWW_WEATHER_PARTLY_CLOUDY: return "晴间云";
    case LVWW_WEATHER_CLOUDY: return "多云";
    case LVWW_WEATHER_FOG: return "雾";
    case LVWW_WEATHER_DRIZZLE: return "毛毛雨";
    case LVWW_WEATHER_RAIN: return "有雨";
    case LVWW_WEATHER_SNOW: return "有雪";
    case LVWW_WEATHER_STORM: return "雷暴";
    default: return "--";
    }
}

static void lvww_set_decimal_label(lv_obj_t *label, const char *prefix,
                                   float value, const char *suffix)
{
    char buffer[64];
    int scaled = (int)(value * 10.0f + (value >= 0.0f ? 0.5f : -0.5f));
    unsigned magnitude = (unsigned)(scaled < 0 ? -scaled : scaled);
    rt_snprintf(buffer, sizeof(buffer), "%s%s%u.%u%s",
                prefix ? prefix : "", scaled < 0 ? "-" : "",
                magnitude / 10u, magnitude % 10u, suffix ? suffix : "");
    lv_label_set_text(label, buffer);
}

static void lvww_epoch_to_calendar(int64_t epoch, int *year, int *month, int *day,
                                   int *hour, int *minute, int *weekday)
{
    int64_t days = epoch / 86400;
    int64_t seconds = epoch % 86400;
    int64_t z, era, doe, yoe, y, doy, mp;

    if (seconds < 0)
    {
        seconds += 86400;
        --days;
    }
    *hour = (int)(seconds / 3600);
    *minute = (int)((seconds % 3600) / 60);
    *weekday = (int)((days + 4) % 7);
    if (*weekday < 0)
        *weekday += 7;

    z = days + 719468;
    era = (z >= 0 ? z : z - 146096) / 146097;
    doe = z - era * 146097;
    yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    y = yoe + era * 400;
    doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    mp = (5 * doy + 2) / 153;
    *day = (int)(doy - (153 * mp + 2) / 5 + 1);
    *month = (int)(mp + (mp < 10 ? 3 : -9));
    *year = (int)(y + (*month <= 2));
}

static uint64_t lvww_utc_now(lvww_ctx_t *ctx)
{
    rt_tick_t elapsed;
    if (!ctx->time_valid)
        return 0;
    elapsed = rt_tick_get() - ctx->utc_tick;
    return ctx->utc_epoch + (uint64_t)(elapsed / RT_TICK_PER_SECOND);
}

static void lvww_refresh_clock(lvww_ctx_t *ctx)
{
    static const char *week[] = {"周日", "周一", "周二", "周三", "周四", "周五", "周六"};
    uint64_t utc;
    int y, m, d, h, min, wd;
    int32_t offset = ctx->cache_store.weather_valid ?
                     ctx->cache_store.weather.utc_offset_seconds : 0;

    utc = lvww_utc_now(ctx);
    if (!utc)
    {
        lv_label_set_text(ctx->home_clock, "--:--");
        lv_label_set_text(ctx->home_date, "等待网络校时");
        return;
    }
    lvww_epoch_to_calendar((int64_t)utc + offset, &y, &m, &d, &h, &min, &wd);
    lv_label_set_text_fmt(ctx->home_clock, "%02d:%02d", h, min);
    lv_label_set_text_fmt(ctx->home_date, "%04d-%02d-%02d  %s", y, m, d, week[wd]);
}

static void lvww_refresh_home(lvww_ctx_t *ctx)
{
    const lvww_weather_t *w = &ctx->cache_store.weather;
    const char *city = ctx->cache_store.city_valid ? ctx->cache_store.city.name : "未选择城市";

    lv_label_set_text(ctx->home_city, city);
    if (ctx->wifi_state == LVWW_WIFI_ONLINE)
    {
        lv_label_set_text(ctx->home_net, "无线在线");
        lv_obj_set_style_text_color(ctx->home_net, lv_color_hex(0x55D6A9), 0);
    }
    else if (ctx->wifi_state == LVWW_WIFI_CONNECTING)
    {
        lv_label_set_text(ctx->home_net, "无线连接中...");
        lv_obj_set_style_text_color(ctx->home_net, lv_color_hex(0xF2C66D), 0);
    }
    else
    {
        lv_label_set_text(ctx->home_net, "无线离线");
        lv_obj_set_style_text_color(ctx->home_net, lv_color_hex(0xF07B88), 0);
    }

    if (!ctx->cache_store.weather_valid)
    {
        lv_obj_set_style_text_font(ctx->home_weather_icon,
                                   ctx->cfg.font_large, 0);
        lv_label_set_text(ctx->home_weather_icon, "--");
        lv_label_set_text(ctx->home_temperature, "--.-");
        lv_label_set_text(ctx->home_apparent, "体感 --.-℃");
        lv_label_set_text(ctx->home_humidity, "湿度 -- %");
        lv_label_set_text(ctx->home_wind, "风速 -- km/h");
        lv_label_set_text(ctx->home_range, "今日 -- / --℃");
        lv_label_set_text(ctx->home_updated, "联网后自动刷新");
    }
    else
    {
        lv_obj_set_style_text_font(ctx->home_weather_icon,
                                   &lvww_font_cjk_32, 0);
        lv_label_set_text(ctx->home_weather_icon, lvww_weather_icon(w->code));
        lvww_set_decimal_label(ctx->home_temperature, "", w->temperature_c, "");
        lvww_set_decimal_label(ctx->home_apparent, "体感 ", w->apparent_c, "℃");
        lv_label_set_text_fmt(ctx->home_humidity, "湿度 %u %%",
                              (unsigned)(w->humidity_percent + 0.5f));
        lvww_set_decimal_label(ctx->home_wind, "风速 ", w->wind_kph, " km/h");
        {
            char high[24];
            char range[56];
            int hi = (int)(w->today_high_c * 10.0f + (w->today_high_c >= 0 ? 0.5f : -0.5f));
            int lo = (int)(w->today_low_c * 10.0f + (w->today_low_c >= 0 ? 0.5f : -0.5f));
            rt_snprintf(high, sizeof(high), "%s%u.%u", hi < 0 ? "-" : "",
                        (unsigned)(hi < 0 ? -hi : hi) / 10u,
                        (unsigned)(hi < 0 ? -hi : hi) % 10u);
            rt_snprintf(range, sizeof(range), "今日 %s / %s%u.%u℃", high,
                        lo < 0 ? "-" : "", (unsigned)(lo < 0 ? -lo : lo) / 10u,
                        (unsigned)(lo < 0 ? -lo : lo) % 10u);
            lv_label_set_text(ctx->home_range, range);
        }
        lv_label_set_text(ctx->home_updated,
                          ctx->wifi_state == LVWW_WIFI_ONLINE ? "天气已更新" : "离线缓存");
    }
    lv_obj_update_layout(ctx->home_temperature);
    lv_obj_align_to(ctx->home_temperature_unit, ctx->home_temperature,
                    LV_ALIGN_OUT_RIGHT_TOP, 4, 4);
    lvww_refresh_clock(ctx);
}

static void lvww_hide_keyboard(lvww_ctx_t *ctx)
{
    lv_obj_t *textarea;

    if (!ctx || !ctx->keyboard)
        return;
    textarea = lv_keyboard_get_textarea(ctx->keyboard);
    if (textarea)
        lv_obj_clear_state(textarea, LV_STATE_FOCUSED);
    lv_keyboard_set_textarea(ctx->keyboard, RT_NULL);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    if (ctx->city_results)
        lv_obj_set_height(ctx->city_results, 276);
}

static void lvww_show_keyboard(lvww_ctx_t *ctx, lv_obj_t *textarea)
{
    lv_obj_t *previous;
    lv_coord_t keyboard_top;
    lv_coord_t results_height;

    if (!ctx || !ctx->keyboard || !textarea)
        return;
    previous = lv_keyboard_get_textarea(ctx->keyboard);
    if (previous && previous != textarea)
        lv_obj_clear_state(previous, LV_STATE_FOCUSED);
    lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ctx->keyboard, textarea);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_obj_clear_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->keyboard);
    lv_obj_update_layout(ctx->keyboard);
    lv_obj_invalidate(ctx->keyboard);
    if (!ctx->editor && ctx->city_results)
    {
        /* Fill all space above the keyboard.  The previous fixed 88 px
         * height left a large empty strip and exposed only one city row. */
        keyboard_top = (lv_coord_t)ctx->cfg.height -
                       lv_obj_get_height(ctx->keyboard);
        results_height = keyboard_top - lv_obj_get_y(ctx->city_results) - 8;
        if (results_height < 62)
            results_height = 62;
        lv_obj_set_height(ctx->city_results, results_height);
        lv_obj_scroll_to_y(ctx->city_results, 0, LV_ANIM_OFF);
    }
}

static void lvww_keyboard_event_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL)
        lvww_hide_keyboard(ctx);
}

static void lvww_textarea_focus_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    lv_event_code_t code = lv_event_get_code(event);
    if (code == LV_EVENT_FOCUSED || code == LV_EVENT_CLICKED)
        lvww_show_keyboard(ctx, lv_event_get_target(event));
}

static void lvww_refresh_nav(lvww_ctx_t *ctx, int page)
{
    int i;

    for (i = 0; i < 3; ++i)
    {
        lv_obj_t *button = ctx->nav_buttons[i];
        lv_obj_t *label;
        rt_bool_t active;

        if (!button)
            continue;
        active = i == page;
        lv_obj_set_style_bg_color(button,
                                  active ? lv_color_hex(0x1C3D68) :
                                           lv_color_hex(0x182338), 0);
        lv_obj_set_style_border_width(button, active ? 3 : 2, 0);
        lv_obj_set_style_border_color(button,
                                      active ? ctx->cfg.accent_color :
                                               lv_color_hex(0x34445F), 0);
        lv_obj_set_style_radius(button, 14, 0);
        label = lv_obj_get_child(button, 0);
        if (label)
            lv_obj_set_style_text_color(label,
                                        active ? lv_color_white() :
                                                 lv_color_hex(0xAEB8CC), 0);
    }
}

static void lvww_show_page(lvww_ctx_t *ctx, int page)
{
    int i;
    for (i = 0; i < 3; ++i)
    {
        if (i == page)
            lv_obj_clear_flag(ctx->pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(ctx->pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lvww_refresh_nav(ctx, page);
    if (page == LVWW_PAGE_CITY && !ctx->editor)
        lvww_show_keyboard(ctx, ctx->city_input);
    else if (!ctx->editor)
        lvww_hide_keyboard(ctx);
    if (page == LVWW_PAGE_WIFI)
        lvww_request_scan(ctx);
}

void lvww_show_home(lvww_ctx_t *ctx)
{
    if (ctx)
        lvww_show_page(ctx, LVWW_PAGE_HOME);
}

void lvww_show_wifi(lvww_ctx_t *ctx)
{
    if (ctx)
        lvww_show_page(ctx, LVWW_PAGE_WIFI);
}

void lvww_show_city(lvww_ctx_t *ctx)
{
    if (ctx)
        lvww_show_page(ctx, LVWW_PAGE_CITY);
}

static void lvww_nav_event_cb(lv_event_t *event)
{
    lvww_binding_t *binding = (lvww_binding_t *)lv_event_get_user_data(event);
    lvww_show_page(binding->ctx, (int)binding->tag);
}

static void lvww_request_scan(lvww_ctx_t *ctx)
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

static void lvww_request_connect(lvww_ctx_t *ctx, const lvww_wifi_credentials_t *credentials)
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

static void lvww_request_disconnect(lvww_ctx_t *ctx)
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

static void lvww_request_weather(lvww_ctx_t *ctx)
{
    int rc;
    if (ctx->wifi_state != LVWW_WIFI_ONLINE || !ctx->cache_store.city_valid ||
        !ctx->ops.weather_fetch || ctx->weather_request_id)
        return;
    ctx->weather_request_id = lvww_next_request(ctx);
    rc = ctx->ops.weather_fetch(ctx->user_ctx, ctx->weather_request_id,
                                &ctx->cache_store.city);
    if (rc != RT_EOK)
    {
        ctx->weather_request_id = 0;
        lvww_show_toast(ctx, "无法启动天气更新", RT_TRUE);
    }
}

static void lvww_request_time(lvww_ctx_t *ctx)
{
    int rc;
    if (ctx->wifi_state != LVWW_WIFI_ONLINE || !ctx->ops.time_sync || ctx->time_request_id)
        return;
    ctx->time_request_id = lvww_next_request(ctx);
    rc = ctx->ops.time_sync(ctx->user_ctx, ctx->time_request_id);
    if (rc != RT_EOK)
    {
        ctx->time_request_id = 0;
        lvww_show_toast(ctx, "无法启动网络校时", RT_TRUE);
    }
}

static rt_bool_t lvww_password_valid(const char *password)
{
    rt_size_t len = rt_strlen(password);
    rt_size_t i;
    if (len >= 8 && len <= 63)
        return RT_TRUE;
    if (len != 64)
        return RT_FALSE;
    for (i = 0; i < len; ++i)
    {
        char c = password[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
              (c >= 'A' && c <= 'F')))
            return RT_FALSE;
    }
    return RT_TRUE;
}

static void lvww_editor_security_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    ctx->editor_secure = !ctx->editor_secure;
    lv_label_set_text(ctx->editor_security_label,
                      ctx->editor_secure ? "加密网络" : "开放网络");
    if (ctx->editor_secure)
    {
        lv_obj_clear_flag(ctx->editor_password, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(ctx->editor_password_label, LV_OBJ_FLAG_HIDDEN);
    }
    else
    {
        lv_obj_add_flag(ctx->editor_password, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->editor_password_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void lvww_editor_show_password_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *button = lv_event_get_target(event);
    rt_bool_t password_mode = lv_textarea_get_password_mode(ctx->editor_password);
    lv_textarea_set_password_mode(ctx->editor_password, !password_mode);
    lv_label_set_text(lv_obj_get_child(button, 0), password_mode ? "隐藏" : "显示");
}

static void lvww_editor_submit_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    lvww_wifi_credentials_t credentials;
    const char *ssid = lv_textarea_get_text(ctx->editor_ssid);
    const char *password = lv_textarea_get_text(ctx->editor_password);

    rt_memset(&credentials, 0, sizeof(credentials));
    if (!ssid[0])
    {
        lvww_show_toast(ctx, "SSID 不能为空", RT_TRUE);
        return;
    }
    if (ctx->editor_saved_index < 0 && lvww_find_profile(ctx, ssid) < 0 &&
        ctx->profile_store.count >= ctx->cfg.max_profiles)
    {
        lvww_show_toast(ctx, "已达到 Wi-Fi 账号数量上限", RT_TRUE);
        return;
    }
    if (ctx->editor_secure && !lvww_password_valid(password))
    {
        lvww_show_toast(ctx, "密码需为 8-63 字符或 64 位十六进制", RT_TRUE);
        return;
    }
    lvww_copy_text(credentials.ssid, sizeof(credentials.ssid), ssid);
    if (ctx->editor_secure)
        lvww_copy_text(credentials.password, sizeof(credentials.password), password);
    credentials.secure = ctx->editor_secure;
    lvww_close_editor(ctx);
    lvww_request_connect(ctx, &credentials);
    lvww_secure_zero(&credentials, sizeof(credentials));
}

static void lvww_editor_delete_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    int index = ctx->editor_saved_index;
    int i;
    char deleted_ssid[LVWW_SSID_MAX_LEN + 1];

    if (index < 0 || index >= ctx->profile_store.count)
        return;
    lvww_copy_text(deleted_ssid, sizeof(deleted_ssid),
                   ctx->profile_store.profiles[index].ssid);
    lvww_secure_zero(&ctx->profile_store.profiles[index],
                     sizeof(ctx->profile_store.profiles[index]));
    for (i = index; i + 1 < ctx->profile_store.count; ++i)
        ctx->profile_store.profiles[i] = ctx->profile_store.profiles[i + 1];
    --ctx->profile_store.count;
    lvww_secure_zero(&ctx->profile_store.profiles[ctx->profile_store.count],
                     sizeof(ctx->profile_store.profiles[0]));
    if (ctx->profile_store.last_success == index)
        ctx->profile_store.last_success = LVWW_INVALID_INDEX;
    else if (ctx->profile_store.last_success > index)
        --ctx->profile_store.last_success;
    lvww_save_profiles(ctx);
    lvww_close_editor(ctx);
    lvww_refresh_wifi(ctx);
    lvww_show_toast(ctx, "已删除 Wi-Fi 账号", RT_FALSE);
    if (ctx->wifi_state == LVWW_WIFI_ONLINE &&
        rt_strncmp(ctx->connected_ssid, deleted_ssid, sizeof(deleted_ssid)) == 0)
        lvww_request_disconnect(ctx);
}

static void lvww_editor_close_cb(lv_event_t *event)
{
    lvww_close_editor((lvww_ctx_t *)lv_event_get_user_data(event));
}

static void lvww_open_editor(lvww_ctx_t *ctx, const char *ssid, rt_bool_t secure,
                             int saved_index)
{
    lv_obj_t *panel;
    lv_obj_t *button;
    const char *password = "";

    lvww_close_editor(ctx);
    ctx->editor_saved_index = saved_index;
    ctx->editor_secure = secure;
    if (saved_index >= 0 && saved_index < ctx->profile_store.count)
        password = ctx->profile_store.profiles[saved_index].password;

    ctx->editor = lv_obj_create(ctx->root);
    lv_obj_set_size(ctx->editor, ctx->cfg.width, ctx->cfg.height);
    lv_obj_set_pos(ctx->editor, 0, 0);
    lv_obj_set_style_bg_color(ctx->editor, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ctx->editor, LV_OPA_70, 0);
    lv_obj_set_style_border_width(ctx->editor, 0, 0);
    lv_obj_set_style_pad_all(ctx->editor, 0, 0);
    lv_obj_clear_flag(ctx->editor, LV_OBJ_FLAG_SCROLLABLE);

    panel = lvww_panel(ctx->editor, ctx->cfg.panel_color, 16);
    lv_obj_set_size(panel, 760, 210);
    lv_obj_set_pos(panel, 20, 8);

    lv_obj_t *title = lvww_label(ctx, panel,
                                 saved_index >= 0 ? "编辑 Wi-Fi 账号" : "添加无线账号",
                                 ctx->cfg.font_ui, lv_color_white());
    lv_obj_set_pos(title, 8, 2);

    button = lvww_button(ctx, panel, "关闭", 72, 42);
    lv_obj_align(button, LV_ALIGN_TOP_RIGHT, 0, -4);
    lv_obj_add_event_cb(button, lvww_editor_close_cb, LV_EVENT_CLICKED, ctx);

    lv_obj_t *ssid_label = lvww_label(ctx, panel, "SSID", ctx->cfg.font_ui,
                                      lv_color_hex(0xAEB8CC));
    lv_obj_set_pos(ssid_label, 8, 52);
    ctx->editor_ssid = lv_textarea_create(panel);
    lv_obj_set_size(ctx->editor_ssid, 300, 48);
    lv_obj_set_pos(ctx->editor_ssid, 70, 42);
    lv_textarea_set_one_line(ctx->editor_ssid, RT_TRUE);
    lv_textarea_set_max_length(ctx->editor_ssid, LVWW_SSID_MAX_LEN);
    lv_textarea_set_text(ctx->editor_ssid, ssid ? ssid : "");
    lv_obj_set_style_text_font(ctx->editor_ssid, ctx->cfg.font_ui, 0);
    lvww_style_textarea(ctx, ctx->editor_ssid);
    lv_obj_add_event_cb(ctx->editor_ssid, lvww_textarea_focus_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->editor_ssid, lvww_textarea_focus_cb, LV_EVENT_CLICKED, ctx);

    button = lvww_button(ctx, panel, secure ? "加密网络" : "开放网络", 126, 48);
    lv_obj_set_pos(button, 386, 42);
    ctx->editor_security_label = lv_obj_get_child(button, 0);
    lv_obj_add_event_cb(button, lvww_editor_security_cb, LV_EVENT_CLICKED, ctx);

    ctx->editor_password_label = lvww_label(ctx, panel, "密码", ctx->cfg.font_ui,
                                            lv_color_hex(0xAEB8CC));
    lv_obj_set_pos(ctx->editor_password_label, 8, 112);
    ctx->editor_password = lv_textarea_create(panel);
    lv_obj_set_size(ctx->editor_password, 300, 48);
    lv_obj_set_pos(ctx->editor_password, 70, 102);
    lv_textarea_set_one_line(ctx->editor_password, RT_TRUE);
    lv_textarea_set_password_mode(ctx->editor_password, RT_TRUE);
    lv_textarea_set_max_length(ctx->editor_password, LVWW_PASSWORD_MAX_LEN);
    lv_textarea_set_text(ctx->editor_password, password);
    lv_obj_set_style_text_font(ctx->editor_password, ctx->cfg.font_ui, 0);
    lvww_style_textarea(ctx, ctx->editor_password);
    lv_obj_add_event_cb(ctx->editor_password, lvww_textarea_focus_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->editor_password, lvww_textarea_focus_cb, LV_EVENT_CLICKED, ctx);

    button = lvww_button(ctx, panel, "显示", 72, 48);
    lv_obj_set_pos(button, 386, 102);
    lv_obj_add_event_cb(button, lvww_editor_show_password_cb, LV_EVENT_CLICKED, ctx);

    button = lvww_button(ctx, panel, "保存并连接", 150, 48);
    lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    lv_obj_add_event_cb(button, lvww_editor_submit_cb, LV_EVENT_CLICKED, ctx);

    if (saved_index >= 0)
    {
        button = lvww_button(ctx, panel, "删除账号", 112, 48);
        lv_obj_set_style_bg_color(button, lv_color_hex(0xA53C4B), 0);
        lv_obj_align(button, LV_ALIGN_BOTTOM_RIGHT, -164, 0);
        lv_obj_add_event_cb(button, lvww_editor_delete_cb, LV_EVENT_CLICKED, ctx);
    }
    if (!secure)
    {
        lv_obj_add_flag(ctx->editor_password, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(ctx->editor_password_label, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_move_foreground(ctx->editor);
    lvww_show_keyboard(ctx, ctx->editor_ssid);
}

static void lvww_close_editor(lvww_ctx_t *ctx)
{
    if (!ctx)
        return;
    lvww_hide_keyboard(ctx);
    if (ctx->editor)
        lv_obj_del(ctx->editor);
    ctx->editor = RT_NULL;
    ctx->editor_ssid = RT_NULL;
    ctx->editor_password = RT_NULL;
    ctx->editor_security_label = RT_NULL;
    ctx->editor_password_label = RT_NULL;
    ctx->editor_saved_index = LVWW_INVALID_INDEX;
}

static void lvww_wifi_row_cb(lv_event_t *event)
{
    lvww_binding_t *binding = (lvww_binding_t *)lv_event_get_user_data(event);
    lvww_ctx_t *ctx = binding->ctx;
    uintptr_t tag = binding->tag;
    int saved_index;
    if (tag & LVWW_WIFI_SAVED_TAG)
    {
        saved_index = (int)(tag & 0xFFu);
        if (saved_index < ctx->profile_store.count)
            lvww_open_editor(ctx, ctx->profile_store.profiles[saved_index].ssid,
                             ctx->profile_store.profiles[saved_index].secure, saved_index);
    }
    else if (tag < ctx->wifi_count)
    {
        saved_index = lvww_find_profile(ctx, ctx->wifi_results[tag].ssid);
        lvww_open_editor(ctx, ctx->wifi_results[tag].ssid, ctx->wifi_results[tag].secure,
                         saved_index);
    }
}

static rt_bool_t lvww_ssid_in_scan(lvww_ctx_t *ctx, const char *ssid)
{
    uint16_t i;
    for (i = 0; i < ctx->wifi_count; ++i)
        if (rt_strncmp(ctx->wifi_results[i].ssid, ssid, LVWW_SSID_MAX_LEN + 1) == 0)
            return RT_TRUE;
    return RT_FALSE;
}

static void lvww_add_wifi_row(lvww_ctx_t *ctx, const char *ssid, int rssi,
                              rt_bool_t secure, rt_bool_t saved, uintptr_t tag)
{
    lvww_binding_t *binding;
    lv_obj_t *row = lvww_panel(ctx->wifi_list, lv_color_hex(0x263148), 10);
    lv_obj_set_width(row, LV_PCT(100));
    lv_obj_set_height(row, 62);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
    binding = &ctx->wifi_bindings[ctx->wifi_binding_count++];
    binding->ctx = ctx;
    binding->tag = tag;
    lv_obj_add_event_cb(row, lvww_wifi_row_cb, LV_EVENT_CLICKED, binding);

    lv_obj_t *name = lvww_label(ctx, row, ssid, ctx->cfg.font_city, lv_color_white());
    lv_obj_set_pos(name, 4, 0);
    lv_obj_t *detail = lvww_label(ctx, row, "", ctx->cfg.font_ui, lv_color_hex(0xAEB8CC));
    if (rssi > -120)
        lv_label_set_text_fmt(detail, "%d dBm  %s%s", rssi,
                              secure ? "加密" : "开放", saved ? "  已保存" : "");
    else
        lv_label_set_text_fmt(detail, "未扫描到  %s", saved ? "已保存" : "");
    lv_obj_set_pos(detail, 4, 27);
    lv_obj_t *hint = lvww_label(ctx, row, "管理", ctx->cfg.font_ui, ctx->cfg.accent_color);
    lv_obj_align(hint, LV_ALIGN_RIGHT_MID, -4, 0);
}

static void lvww_refresh_wifi(lvww_ctx_t *ctx)
{
    uint16_t i;
    lv_obj_clean(ctx->wifi_list);
    ctx->wifi_binding_count = 0;
    if (!ctx->wifi_count && !ctx->profile_store.count)
    {
        lv_obj_t *empty = lvww_label(ctx, ctx->wifi_list, "暂无热点，点击扫描或手动添加",
                                     ctx->cfg.font_ui, lv_color_hex(0xAEB8CC));
        lv_obj_center(empty);
    }
    for (i = 0; i < ctx->wifi_count; ++i)
    {
        int profile = lvww_find_profile(ctx, ctx->wifi_results[i].ssid);
        lvww_add_wifi_row(ctx, ctx->wifi_results[i].ssid, ctx->wifi_results[i].rssi,
                          ctx->wifi_results[i].secure, profile >= 0, i);
    }
    for (i = 0; i < ctx->profile_store.count; ++i)
    {
        if (!lvww_ssid_in_scan(ctx, ctx->profile_store.profiles[i].ssid))
            lvww_add_wifi_row(ctx, ctx->profile_store.profiles[i].ssid, -127,
                              ctx->profile_store.profiles[i].secure, RT_TRUE,
                              LVWW_WIFI_SAVED_TAG | i);
    }

    if (ctx->wifi_state == LVWW_WIFI_ONLINE)
        lv_label_set_text_fmt(ctx->wifi_status, "已连接：%s", ctx->connected_ssid);
    else if (ctx->wifi_state == LVWW_WIFI_SCANNING)
        lv_label_set_text(ctx->wifi_status, "正在扫描热点...");
    else if (ctx->wifi_state == LVWW_WIFI_CONNECTING)
        lv_label_set_text(ctx->wifi_status, "正在连接...");
    else
        lv_label_set_text(ctx->wifi_status, "当前未连接");
}

static void lvww_scan_button_cb(lv_event_t *event)
{
    lvww_request_scan((lvww_ctx_t *)lv_event_get_user_data(event));
}

static void lvww_add_button_cb(lv_event_t *event)
{
    lvww_open_editor((lvww_ctx_t *)lv_event_get_user_data(event), "", RT_TRUE,
                     LVWW_INVALID_INDEX);
}

static void lvww_disconnect_button_cb(lv_event_t *event)
{
    lvww_request_disconnect((lvww_ctx_t *)lv_event_get_user_data(event));
}

static void lvww_city_result_cb(lv_event_t *event)
{
    lvww_binding_t *binding = (lvww_binding_t *)lv_event_get_user_data(event);
    lvww_ctx_t *ctx = binding->ctx;
    uintptr_t index = binding->tag;
    if (index >= ctx->city_count)
        return;
    ctx->cache_store.city = ctx->city_candidates[index];
    ctx->cache_store.city_valid = 1;
    ctx->cache_store.weather_valid = 0;
    lvww_save_cache(ctx);
    lvww_hide_keyboard(ctx);
    lvww_refresh_home(ctx);
    lvww_show_toast(ctx, "城市已更新", RT_FALSE);
    lvww_show_home(ctx);
    lvww_request_weather(ctx);
}

static void lvww_refresh_city_results(lvww_ctx_t *ctx)
{
    uint16_t i;
    lv_obj_clean(ctx->city_results);
    if (!ctx->city_count)
    {
        lv_obj_t *empty = lvww_label(ctx, ctx->city_results, "没有匹配城市",
                                     ctx->cfg.font_ui, lv_color_hex(0xAEB8CC));
        lv_obj_center(empty);
        return;
    }
    for (i = 0; i < ctx->city_count; ++i)
    {
        lvww_binding_t *binding = &ctx->city_bindings[i];
        lv_obj_t *row = lvww_panel(ctx->city_results, lv_color_hex(0x263148), 10);
        lv_obj_set_width(row, LV_PCT(100));
        lv_obj_set_height(row, 62);
        lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
        binding->ctx = ctx;
        binding->tag = i;
        lv_obj_add_event_cb(row, lvww_city_result_cb, LV_EVENT_CLICKED, binding);
        lv_obj_t *name = lvww_label(ctx, row, ctx->city_candidates[i].name,
                                    ctx->cfg.font_city, lv_color_white());
        lv_obj_set_pos(name, 4, 0);
        lv_obj_t *detail = lvww_label(ctx, row, "", ctx->cfg.font_city,
                                      lv_color_hex(0xAEB8CC));
        lv_label_set_text_fmt(detail, "%s  %s", ctx->city_candidates[i].admin,
                              ctx->city_candidates[i].country);
        lv_obj_set_pos(detail, 4, 27);
    }
}

static void lvww_search_timer_cb(lv_timer_t *timer)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)timer->user_data;
    const char *query = lv_textarea_get_text(ctx->city_input);
    int rc;
    lv_timer_pause(timer);
    if (rt_strlen(query) < 2)
        return;
    if (!ctx->ops.city_search)
    {
        lvww_show_toast(ctx, "城市查询适配器不可用", RT_TRUE);
        return;
    }
    if (ctx->city_request_id && ctx->ops.cancel)
        ctx->ops.cancel(ctx->user_ctx, LVWW_OP_CITY_SEARCH, ctx->city_request_id);
    ctx->city_request_id = lvww_next_request(ctx);
    lv_obj_clean(ctx->city_results);
    lv_obj_t *loading = lvww_label(ctx, ctx->city_results, "正在查询城市...",
                                   ctx->cfg.font_ui, lv_color_hex(0xAEB8CC));
    lv_obj_center(loading);
    rc = ctx->ops.city_search(ctx->user_ctx, ctx->city_request_id, query);
    if (rc != RT_EOK)
    {
        ctx->city_request_id = 0;
        lvww_show_toast(ctx, "无法启动城市查询", RT_TRUE);
    }
}

static void lvww_city_input_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_VALUE_CHANGED)
    {
        if (rt_strlen(lv_textarea_get_text(ctx->city_input)) < 2)
        {
            if (ctx->city_request_id && ctx->ops.cancel)
                ctx->ops.cancel(ctx->user_ctx, LVWW_OP_CITY_SEARCH,
                                ctx->city_request_id);
            ctx->city_request_id = 0;
            ctx->city_count = (uint16_t)lvww_city_catalog_search(
                "", ctx->city_candidates, ctx->cfg.max_city_results);
            lvww_refresh_city_results(ctx);
            lv_timer_pause(ctx->search_timer);
        }
        else
        {
            lv_timer_set_period(ctx->search_timer, 500);
            lv_timer_reset(ctx->search_timer);
            lv_timer_resume(ctx->search_timer);
        }
    }
}

static void lvww_upsert_pending_profile(lvww_ctx_t *ctx)
{
    int index;
    if (!ctx->pending_valid)
        return;
    index = lvww_find_profile(ctx, ctx->pending_credentials.ssid);
    if (index < 0)
    {
        if (ctx->profile_store.count >= ctx->cfg.max_profiles)
            return;
        index = ctx->profile_store.count++;
    }
    ctx->profile_store.profiles[index] = ctx->pending_credentials;
    ctx->profile_store.last_success = (int8_t)index;
    lvww_save_profiles(ctx);
    lvww_secure_zero(&ctx->pending_credentials, sizeof(ctx->pending_credentials));
    ctx->pending_valid = RT_FALSE;
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
        lvww_show_toast(ctx, "Wi-Fi 已连接", RT_FALSE);
        lvww_request_time(ctx);
        lvww_request_weather(ctx);
    }
    else if (ctx->wifi_state == LVWW_WIFI_OFFLINE)
    {
        ctx->connected_ssid[0] = '\0';
        ctx->connect_request_id = 0;
        ctx->disconnect_request_id = 0;
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
    case LVWW_OP_WEATHER_FETCH: ctx->weather_request_id = 0; break;
    case LVWW_OP_TIME_SYNC: ctx->time_request_id = 0; break;
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
        lvww_save_cache(ctx);
        lvww_refresh_home(ctx);
        break;
    case LVWW_EVT_TIME_RESULT:
        ctx->utc_epoch = event->data.time.utc_epoch;
        ctx->utc_tick = rt_tick_get();
        ctx->time_tick = ctx->utc_tick;
        ctx->time_valid = RT_TRUE;
        ctx->time_request_id = 0;
        lvww_refresh_clock(ctx);
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

static void lvww_build_home(lvww_ctx_t *ctx)
{
    lv_obj_t *page = ctx->pages[LVWW_PAGE_HOME];
    lv_obj_t *weather = lvww_panel(page, ctx->cfg.panel_color, 18);
    lv_obj_set_size(weather, 470, 330);
    lv_obj_set_pos(weather, 14, 14);

    ctx->home_weather_icon = lvww_label(ctx, weather, "--", ctx->cfg.font_large,
                                        ctx->cfg.accent_color);
    lv_obj_set_pos(ctx->home_weather_icon, 18, 30);
    ctx->home_temperature = lvww_label(ctx, weather, "--.-", ctx->cfg.font_large,
                                       lv_color_white());
    lv_obj_set_pos(ctx->home_temperature, 178, 18);
    ctx->home_temperature_unit = lvww_label(ctx, weather, "℃", &lvww_font_cjk_32,
                                            lv_color_white());
    lv_obj_align_to(ctx->home_temperature_unit, ctx->home_temperature,
                    LV_ALIGN_OUT_RIGHT_TOP, 4, 4);
    ctx->home_apparent = lvww_label(ctx, weather, "体感 --.-℃", ctx->cfg.font_ui,
                                    lv_color_white());
    ctx->home_humidity = lvww_label(ctx, weather, "湿度 -- %", ctx->cfg.font_ui,
                                    lv_color_white());
    ctx->home_wind = lvww_label(ctx, weather, "风速 -- km/h", ctx->cfg.font_ui,
                                lv_color_white());
    ctx->home_range = lvww_label(ctx, weather, "今日 -- / --℃", ctx->cfg.font_ui,
                                 lv_color_white());
    lv_obj_set_pos(ctx->home_apparent, 22, 150);
    lv_obj_set_pos(ctx->home_humidity, 238, 150);
    lv_obj_set_pos(ctx->home_wind, 22, 205);
    lv_obj_set_pos(ctx->home_range, 238, 205);
    ctx->home_updated = lvww_label(ctx, weather, "联网后自动刷新", ctx->cfg.font_ui,
                                   lv_color_hex(0x8591A8));
    lv_obj_align(ctx->home_updated, LV_ALIGN_BOTTOM_LEFT, 10, -4);

    lv_obj_t *info = lvww_panel(page, lv_color_hex(0x1C2639), 18);
    lv_obj_set_size(info, 288, 330);
    lv_obj_set_pos(info, 498, 14);
    ctx->home_city = lvww_label(ctx, info, "未选择城市", ctx->cfg.font_city,
                                lv_color_white());
    lv_obj_set_width(ctx->home_city, 260);
    lv_label_set_long_mode(ctx->home_city, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(ctx->home_city, 4, 4);
    ctx->home_clock = lvww_label(ctx, info, "--:--", ctx->cfg.font_large,
                                 lv_color_white());
    lv_obj_align(ctx->home_clock, LV_ALIGN_TOP_MID, 0, 70);
    ctx->home_date = lvww_label(ctx, info, "等待网络校时", &lvww_font_cjk_16,
                                lv_color_hex(0xAEB8CC));
    lv_obj_align(ctx->home_date, LV_ALIGN_TOP_MID, 0, 140);
    ctx->home_net = lvww_label(ctx, info, "无线离线", ctx->cfg.font_ui,
                               lv_color_hex(0xF07B88));
    lv_obj_align(ctx->home_net, LV_ALIGN_BOTTOM_MID, 0, -28);
}

static void lvww_build_wifi(lvww_ctx_t *ctx)
{
    lv_obj_t *page = ctx->pages[LVWW_PAGE_WIFI];
    lv_obj_t *title = lvww_label(ctx, page, "无线网络管理", ctx->cfg.font_ui,
                                 lv_color_white());
    lv_obj_set_pos(title, 18, 15);
    ctx->wifi_status = lvww_label(ctx, page, "当前未连接", ctx->cfg.font_ui,
                                  lv_color_hex(0xAEB8CC));
    lv_obj_set_pos(ctx->wifi_status, 200, 15);

    lv_obj_t *button = lvww_button(ctx, page, "扫描", 82, 48);
    lv_obj_set_pos(button, 520, 5);
    lv_obj_add_event_cb(button, lvww_scan_button_cb, LV_EVENT_CLICKED, ctx);
    button = lvww_button(ctx, page, "添加", 82, 48);
    lv_obj_set_pos(button, 610, 5);
    lv_obj_add_event_cb(button, lvww_add_button_cb, LV_EVENT_CLICKED, ctx);
    button = lvww_button(ctx, page, "断开", 82, 48);
    lv_obj_set_pos(button, 700, 5);
    lv_obj_add_event_cb(button, lvww_disconnect_button_cb, LV_EVENT_CLICKED, ctx);

    ctx->wifi_list = lv_obj_create(page);
    lv_obj_set_size(ctx->wifi_list, 772, 294);
    lv_obj_set_pos(ctx->wifi_list, 14, 60);
    lv_obj_set_style_bg_opa(ctx->wifi_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->wifi_list, 0, 0);
    lv_obj_set_style_pad_all(ctx->wifi_list, 0, 0);
    lv_obj_set_style_pad_row(ctx->wifi_list, 8, 0);
    lv_obj_set_flex_flow(ctx->wifi_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(ctx->wifi_list, LV_SCROLLBAR_MODE_AUTO);
}

static void lvww_build_city(lvww_ctx_t *ctx)
{
    lv_obj_t *page = ctx->pages[LVWW_PAGE_CITY];
    lv_obj_t *title = lvww_label(ctx, page, "城市与时区", ctx->cfg.font_ui,
                                 lv_color_white());
    lv_obj_set_pos(title, 18, 15);
    ctx->city_input = lv_textarea_create(page);
    lv_obj_set_size(ctx->city_input, 668, 42);
    lv_obj_set_pos(ctx->city_input, 112, 10);
    lv_textarea_set_one_line(ctx->city_input, RT_TRUE);
    lv_textarea_set_max_length(ctx->city_input, 40);
    lv_textarea_set_placeholder_text(ctx->city_input, "输入城市中文名或拼音");
    lv_obj_set_style_text_font(ctx->city_input, ctx->cfg.font_city, 0);
    lvww_style_textarea(ctx, ctx->city_input);
    lv_obj_add_event_cb(ctx->city_input, lvww_textarea_focus_cb, LV_EVENT_FOCUSED, ctx);
    lv_obj_add_event_cb(ctx->city_input, lvww_textarea_focus_cb, LV_EVENT_CLICKED, ctx);
    lv_obj_add_event_cb(ctx->city_input, lvww_city_input_cb, LV_EVENT_VALUE_CHANGED, ctx);

    ctx->city_results = lv_obj_create(page);
    lv_obj_set_size(ctx->city_results, 772, 276);
    lv_obj_set_pos(ctx->city_results, 14, 68);
    lv_obj_set_style_bg_opa(ctx->city_results, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(ctx->city_results, 0, 0);
    lv_obj_set_style_pad_all(ctx->city_results, 0, 0);
    lv_obj_set_style_pad_row(ctx->city_results, 8, 0);
    lv_obj_set_flex_flow(ctx->city_results, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scrollbar_mode(ctx->city_results, LV_SCROLLBAR_MODE_AUTO);
    ctx->city_count = (uint16_t)lvww_city_catalog_search(
        "", ctx->city_candidates, ctx->cfg.max_city_results);
    lvww_refresh_city_results(ctx);
}

static void lvww_build_ui(lvww_ctx_t *ctx, lv_obj_t *parent)
{
    int i;
    lv_obj_t *nav;
    static const char *nav_text[] = {"首页", "无线", "城市"};

    ctx->root = lv_obj_create(parent);
    lv_obj_set_size(ctx->root, ctx->cfg.width, ctx->cfg.height);
    lv_obj_set_pos(ctx->root, 0, 0);
    lv_obj_set_style_bg_color(ctx->root, ctx->cfg.background_color, 0);
    lv_obj_set_style_bg_opa(ctx->root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->root, 0, 0);
    lv_obj_set_style_pad_all(ctx->root, 0, 0);
    lv_obj_clear_flag(ctx->root, LV_OBJ_FLAG_SCROLLABLE);

    for (i = 0; i < 3; ++i)
    {
        ctx->pages[i] = lv_obj_create(ctx->root);
        lv_obj_set_size(ctx->pages[i], ctx->cfg.width, 360);
        lv_obj_set_pos(ctx->pages[i], 0, 0);
        lv_obj_set_style_bg_opa(ctx->pages[i], LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(ctx->pages[i], 0, 0);
        lv_obj_set_style_pad_all(ctx->pages[i], 0, 0);
        lv_obj_clear_flag(ctx->pages[i], LV_OBJ_FLAG_SCROLLABLE);
        if (i != LVWW_PAGE_HOME)
            lv_obj_add_flag(ctx->pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    lvww_build_home(ctx);
    lvww_build_wifi(ctx);
    lvww_build_city(ctx);

    nav = lv_obj_create(ctx->root);
    lv_obj_set_size(nav, ctx->cfg.width, 120);
    lv_obj_align(nav, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(nav, lv_color_hex(0x121A29), 0);
    lv_obj_set_style_border_width(nav, 0, 0);
    lv_obj_set_style_border_width(nav, 1, LV_PART_MAIN);
    lv_obj_set_style_border_side(nav, LV_BORDER_SIDE_TOP, LV_PART_MAIN);
    lv_obj_set_style_border_color(nav, lv_color_hex(0x2C3A52), LV_PART_MAIN);
    lv_obj_set_style_pad_all(nav, 10, 0);
    lv_obj_set_style_pad_column(nav, 8, 0);
    lv_obj_set_flex_flow(nav, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(nav, LV_FLEX_ALIGN_SPACE_EVENLY,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(nav, LV_OBJ_FLAG_SCROLLABLE);
    for (i = 0; i < 3; ++i)
    {
        lv_obj_t *button = lvww_button(ctx, nav, nav_text[i], 245, 88);
        ctx->nav_buttons[i] = button;
        ctx->nav_bindings[i].ctx = ctx;
        ctx->nav_bindings[i].tag = i;
        lv_obj_add_event_cb(button, lvww_nav_event_cb, LV_EVENT_CLICKED,
                            &ctx->nav_bindings[i]);
    }
    lvww_refresh_nav(ctx, LVWW_PAGE_HOME);

    /*
     * Keep the keyboard in this component's object tree.  A keyboard on
     * lv_layer_top() is owned by the display instead of this UI and can be
     * missed by display/layer lifecycle changes.  lvww_show_keyboard() moves
     * this floating child above the navigation bar and editor overlay every
     * time it is shown.
     */
    ctx->keyboard = lv_keyboard_create(ctx->root);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(ctx->keyboard, ctx->cfg.width, 205);
    /* lv_keyboard_create() uses bottom alignment by default.  Setting y to
     * height - keyboard_height would therefore be applied as an additional
     * alignment offset and place the keyboard below the screen. */
    lv_obj_align(ctx->keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->keyboard, ctx->cfg.panel_color, 0);
    lv_obj_set_style_bg_opa(ctx->keyboard, LV_OPA_COVER, 0);
    lv_obj_set_style_text_font(ctx->keyboard, ctx->cfg.font_ui, 0);
    lv_obj_set_style_text_color(ctx->keyboard, lv_color_white(), LV_PART_ITEMS);
    lv_obj_set_style_text_color(ctx->keyboard, lv_color_white(),
                                LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_set_style_text_color(ctx->keyboard, lv_color_white(),
                                LV_PART_ITEMS | LV_STATE_CHECKED);
    lv_obj_add_event_cb(ctx->keyboard, lvww_keyboard_event_cb, LV_EVENT_ALL, ctx);
    lv_obj_add_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);

    ctx->toast = lv_label_create(ctx->root);
    lv_obj_set_width(ctx->toast, LV_SIZE_CONTENT);
    lv_obj_set_style_max_width(ctx->toast, 600, 0);
    lv_obj_set_style_text_font(ctx->toast, ctx->cfg.font_ui, 0);
    lv_obj_set_style_text_color(ctx->toast, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(ctx->toast, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(ctx->toast, 10, 0);
    lv_obj_set_style_pad_left(ctx->toast, 18, 0);
    lv_obj_set_style_pad_right(ctx->toast, 18, 0);
    lv_obj_set_style_pad_top(ctx->toast, 12, 0);
    lv_obj_set_style_pad_bottom(ctx->toast, 12, 0);
    lv_obj_align(ctx->toast, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_add_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
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
    if (ctx->keyboard)
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
