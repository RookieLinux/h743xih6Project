#ifndef LV_WIFI_WEATHER_H
#define LV_WIFI_WEATHER_H

#include <rtthread.h>
#include <lvgl.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LVWW_VERSION_MAJOR             1
#define LVWW_VERSION_MINOR             0
#define LVWW_MAX_PROFILES              8
#define LVWW_MAX_WIFI_RESULTS          20
#define LVWW_MAX_CITY_RESULTS          8
#define LVWW_SSID_MAX_LEN              32
#define LVWW_PASSWORD_MAX_LEN          64
#define LVWW_CITY_ID_MAX_LEN           31
#define LVWW_CITY_NAME_MAX_LEN         47
#define LVWW_TIMEZONE_MAX_LEN          39
#define LVWW_ERROR_TEXT_MAX_LEN        63

#define LVWW_KV_FLAG_SECRET            (1u << 0)

typedef struct lvww_ctx lvww_ctx_t;

LV_FONT_DECLARE(lvww_font_cjk_16)
LV_FONT_DECLARE(lvww_font_cjk_32)

typedef enum
{
    LVWW_WIFI_OFFLINE = 0,
    LVWW_WIFI_SCANNING,
    LVWW_WIFI_CONNECTING,
    LVWW_WIFI_ONLINE,
    LVWW_WIFI_DISCONNECTING
} lvww_wifi_state_t;

typedef enum
{
    LVWW_WIFI_REASON_NONE = 0,
    LVWW_WIFI_REASON_AUTH,
    LVWW_WIFI_REASON_NOT_FOUND,
    LVWW_WIFI_REASON_TIMEOUT,
    LVWW_WIFI_REASON_DROPPED,
    LVWW_WIFI_REASON_UNSUPPORTED,
    LVWW_WIFI_REASON_OTHER
} lvww_wifi_reason_t;

typedef enum
{
    LVWW_WEATHER_CLEAR = 0,
    LVWW_WEATHER_PARTLY_CLOUDY,
    LVWW_WEATHER_CLOUDY,
    LVWW_WEATHER_FOG,
    LVWW_WEATHER_DRIZZLE,
    LVWW_WEATHER_RAIN,
    LVWW_WEATHER_SNOW,
    LVWW_WEATHER_STORM,
    LVWW_WEATHER_UNKNOWN
} lvww_weather_code_t;

typedef enum
{
    LVWW_OP_NONE = 0,
    LVWW_OP_WIFI_SCAN,
    LVWW_OP_WIFI_CONNECT,
    LVWW_OP_WIFI_DISCONNECT,
    LVWW_OP_CITY_SEARCH,
    LVWW_OP_WEATHER_FETCH,
    LVWW_OP_TIME_SYNC,
    LVWW_OP_STORAGE
} lvww_operation_t;

typedef struct
{
    char ssid[LVWW_SSID_MAX_LEN + 1];
    int16_t rssi;
    uint8_t secure;
} lvww_wifi_ap_t;

typedef struct
{
    char ssid[LVWW_SSID_MAX_LEN + 1];
    char password[LVWW_PASSWORD_MAX_LEN + 1];
    uint8_t secure;
} lvww_wifi_credentials_t;

typedef struct
{
    char id[LVWW_CITY_ID_MAX_LEN + 1];
    char name[LVWW_CITY_NAME_MAX_LEN + 1];
    char admin[LVWW_CITY_NAME_MAX_LEN + 1];
    char country[LVWW_CITY_NAME_MAX_LEN + 1];
    char timezone[LVWW_TIMEZONE_MAX_LEN + 1];
    double latitude;
    double longitude;
} lvww_city_t;

typedef struct
{
    lvww_weather_code_t code;
    float temperature_c;
    float apparent_c;
    float humidity_percent;
    float wind_kph;
    float today_high_c;
    float today_low_c;
    int32_t utc_offset_seconds;
    uint64_t observed_utc;
} lvww_weather_t;

typedef enum
{
    LVWW_EVT_WIFI_SCAN_RESULT = 0,
    LVWW_EVT_WIFI_STATE,
    LVWW_EVT_CITY_SEARCH_RESULT,
    LVWW_EVT_WEATHER_RESULT,
    LVWW_EVT_TIME_RESULT,
    LVWW_EVT_ERROR
} lvww_event_type_t;

typedef struct
{
    lvww_event_type_t type;
    uint32_t request_id;
    union
    {
        struct
        {
            uint16_t count;
            lvww_wifi_ap_t items[LVWW_MAX_WIFI_RESULTS];
        } wifi_scan;
        struct
        {
            lvww_wifi_state_t state;
            lvww_wifi_reason_t reason;
            char ssid[LVWW_SSID_MAX_LEN + 1];
        } wifi_state;
        struct
        {
            uint16_t count;
            lvww_city_t items[LVWW_MAX_CITY_RESULTS];
        } city_search;
        lvww_weather_t weather;
        struct
        {
            uint64_t utc_epoch;
        } time;
        struct
        {
            lvww_operation_t operation;
            int code;
            char text[LVWW_ERROR_TEXT_MAX_LEN + 1];
        } error;
    } data;
} lvww_event_t;

typedef struct
{
    uint16_t width;
    uint16_t height;
    uint8_t max_profiles;
    uint8_t max_wifi_results;
    uint8_t max_city_results;
    uint32_t weather_refresh_seconds;
    uint32_t time_sync_seconds;
    lv_color_t background_color;
    lv_color_t panel_color;
    lv_color_t accent_color;
    const lv_font_t *font_ui;
    const lv_font_t *font_large;
    const lv_font_t *font_city;
    lvww_city_t default_city;
} lvww_config_t;

typedef struct
{
    int (*wifi_scan)(void *user_ctx, uint32_t request_id);
    int (*wifi_connect)(void *user_ctx, uint32_t request_id,
                        const lvww_wifi_credentials_t *credentials);
    int (*wifi_disconnect)(void *user_ctx, uint32_t request_id);
    int (*city_search)(void *user_ctx, uint32_t request_id, const char *query);
    int (*weather_fetch)(void *user_ctx, uint32_t request_id, const lvww_city_t *city);
    int (*time_sync)(void *user_ctx, uint32_t request_id);
    void (*cancel)(void *user_ctx, lvww_operation_t operation, uint32_t request_id);
    int (*kv_read)(void *user_ctx, const char *key, void *buffer, rt_size_t *length);
    int (*kv_write)(void *user_ctx, const char *key, const void *data,
                    rt_size_t length, uint32_t flags);
    int (*kv_delete)(void *user_ctx, const char *key);
} lvww_port_ops_t;

void lvww_config_init(lvww_config_t *config);

/*
 * Search the built-in, UTF-8 common-city catalog.  Passing an empty query
 * returns the default list in display order.  This is also used by ports to
 * avoid ambiguous online geocoding results for common Chinese cities.
 */
rt_size_t lvww_city_catalog_search(const char *query,
                                   lvww_city_t *cities,
                                   rt_size_t capacity);

lvww_ctx_t *lvww_create(lv_obj_t *parent,
                        const lvww_config_t *config,
                        const lvww_port_ops_t *port_ops,
                        void *user_ctx);

void lvww_destroy(lvww_ctx_t *ctx);

/* Thread-safe. The event is copied before this function returns. */
int lvww_post_event(lvww_ctx_t *ctx, const lvww_event_t *event);

/* Optional helpers for applications that want to drive the visible page. */
void lvww_show_home(lvww_ctx_t *ctx);
void lvww_show_wifi(lvww_ctx_t *ctx);
void lvww_show_city(lvww_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
