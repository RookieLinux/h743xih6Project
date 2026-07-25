#ifndef LVWW_PRIVATE_H
#define LVWW_PRIVATE_H

#include "lv_wifi_weather.h"

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
    lv_obj_t *pinyin_bar;
    lv_obj_t *pinyin_ime;
    lv_obj_t *pinyin_candidates;
    lv_obj_t *english_candidates;
    lv_obj_t *pinyin_toggle;
    lv_obj_t *pinyin_toggle_label;
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
    rt_bool_t pinyin_enabled;
    uint8_t english_prefix_length;
    char english_candidate_text[5][24];
    const char *english_candidate_map[6];
};

enum
{
    LVWW_PAGE_HOME = 0,
    LVWW_PAGE_WIFI,
    LVWW_PAGE_CITY
};

void lvww_secure_zero(void *data, rt_size_t length);
uint32_t lvww_next_request(lvww_ctx_t *ctx);
void lvww_copy_text(char *dst, rt_size_t dst_size, const char *src);

int lvww_find_profile(lvww_ctx_t *ctx, const char *ssid);
void lvww_save_profiles(lvww_ctx_t *ctx);
void lvww_save_cache(lvww_ctx_t *ctx);
void lvww_load_store(lvww_ctx_t *ctx);
void lvww_upsert_pending_profile(lvww_ctx_t *ctx);

void lvww_show_toast(lvww_ctx_t *ctx, const char *text, rt_bool_t error);
void lvww_refresh_clock(lvww_ctx_t *ctx);
void lvww_refresh_home(lvww_ctx_t *ctx);
void lvww_refresh_wifi(lvww_ctx_t *ctx);
void lvww_refresh_city_results(lvww_ctx_t *ctx);
void lvww_close_editor(lvww_ctx_t *ctx);
void lvww_build_ui(lvww_ctx_t *ctx, lv_obj_t *parent);
void lvww_toast_hide_cb(lv_timer_t *timer);
void lvww_search_timer_cb(lv_timer_t *timer);

void lvww_request_scan(lvww_ctx_t *ctx);
void lvww_request_connect(lvww_ctx_t *ctx,
                          const lvww_wifi_credentials_t *credentials);
void lvww_request_disconnect(lvww_ctx_t *ctx);
void lvww_request_weather(lvww_ctx_t *ctx);
void lvww_request_time(lvww_ctx_t *ctx);

#endif
