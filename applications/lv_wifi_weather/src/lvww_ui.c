#include "lvww_private.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define LVWW_KEYBOARD_HEIGHT        205
#define LVWW_PINYIN_BAR_HEIGHT       40
#define LVWW_PINYIN_TOGGLE_WIDTH     80
#define LVWW_PINYIN_MAX_SYLLABLE      6
#define LVWW_EN_CANDIDATE_COUNT        5

static const char *const lvww_english_words[] = {
    "beijing", "changchun", "changsha", "chengdu", "chongqing",
    "dalian", "dongguan", "foshan", "fuzhou", "guangzhou",
    "guiyang", "haikou", "hangzhou", "harbin", "hefei",
    "home", "hongkong", "jinan", "kunming", "lanzhou",
    "macau", "nanchang", "nanjing", "nanning", "network",
    "ningbo", "office", "qingdao", "shanghai", "shenyang",
    "shenzhen", "shijiazhuang", "suzhou", "tianjin", "wuhan",
    "wifi", "wuxi", "xiamen", "xian", "zhengzhou", "zhuhai"
};

void lvww_copy_text(char *dst, rt_size_t dst_size, const char *src)
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

void lvww_toast_hide_cb(lv_timer_t *timer)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)timer->user_data;
    if (ctx && ctx->toast)
        lv_obj_add_flag(ctx->toast, LV_OBJ_FLAG_HIDDEN);
    lv_timer_pause(timer);
}

void lvww_show_toast(lvww_ctx_t *ctx, const char *text, rt_bool_t error)
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

void lvww_refresh_clock(lvww_ctx_t *ctx)
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

void lvww_refresh_home(lvww_ctx_t *ctx)
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

static void lvww_pinyin_reset(lvww_ctx_t *ctx)
{
#if LV_USE_IME_PINYIN
    lv_ime_pinyin_t *ime;

    if (!ctx || !ctx->pinyin_ime)
        return;
    ime = (lv_ime_pinyin_t *)ctx->pinyin_ime;
    ime->ta_count = 0;
    ime->cand_num = 0;
    ime->py_page = 0;
    rt_memset(ime->input_char, 0, sizeof(ime->input_char));
    if (ctx->pinyin_candidates)
        lv_obj_add_flag(ctx->pinyin_candidates, LV_OBJ_FLAG_HIDDEN);
#else
    (void)ctx;
#endif
}

static rt_bool_t lvww_ascii_word_char(char value)
{
    return (value >= 'a' && value <= 'z') ||
           (value >= 'A' && value <= 'Z');
}

static int lvww_ascii_lower(int value)
{
    return value >= 'A' && value <= 'Z' ? value + ('a' - 'A') : value;
}

static rt_bool_t lvww_english_starts_with(const char *word,
                                          const char *prefix,
                                          rt_size_t prefix_length)
{
    rt_size_t i;
    for (i = 0; i < prefix_length; ++i)
    {
        if (!word[i] ||
            lvww_ascii_lower((unsigned char)word[i]) !=
            lvww_ascii_lower((unsigned char)prefix[i]))
            return RT_FALSE;
    }
    return RT_TRUE;
}

static void lvww_english_candidates_hide(lvww_ctx_t *ctx)
{
    if (!ctx)
        return;
    ctx->english_prefix_length = 0;
    if (ctx->english_candidates)
        lv_obj_add_flag(ctx->english_candidates, LV_OBJ_FLAG_HIDDEN);
}

static void lvww_english_candidates_update(lvww_ctx_t *ctx)
{
    lv_obj_t *textarea;
    const char *text;
    rt_size_t length;
    rt_size_t start;
    rt_size_t prefix_length;
    rt_size_t i;
    uint8_t count = 0;

    if (!ctx || !ctx->english_candidates || ctx->pinyin_enabled ||
        !ctx->keyboard)
        return;
    textarea = lv_keyboard_get_textarea(ctx->keyboard);
    if (!textarea || textarea == ctx->editor_password)
    {
        lvww_english_candidates_hide(ctx);
        return;
    }

    text = lv_textarea_get_text(textarea);
    length = text ? rt_strlen(text) : 0;
    if (lv_textarea_get_cursor_pos(textarea) !=
        _lv_txt_get_encoded_length(text ? text : ""))
    {
        lvww_english_candidates_hide(ctx);
        return;
    }
    start = length;
    while (start > 0 && lvww_ascii_word_char(text[start - 1]))
        --start;
    prefix_length = length - start;
    if (prefix_length < 2 || prefix_length > UINT8_MAX)
    {
        lvww_english_candidates_hide(ctx);
        return;
    }

    for (i = 0;
         i < sizeof(lvww_english_words) / sizeof(lvww_english_words[0]) &&
         count < LVWW_EN_CANDIDATE_COUNT;
         ++i)
    {
        if (!lvww_english_starts_with(lvww_english_words[i],
                                      text + start, prefix_length))
            continue;
        lvww_copy_text(ctx->english_candidate_text[count],
                       sizeof(ctx->english_candidate_text[count]),
                       lvww_english_words[i]);
        ++count;
    }
    if (!count)
    {
        lvww_english_candidates_hide(ctx);
        return;
    }

    for (i = 0; i < LVWW_EN_CANDIDATE_COUNT; ++i)
        ctx->english_candidate_map[i] =
            i < count ? ctx->english_candidate_text[i] : "";
    ctx->english_candidate_map[LVWW_EN_CANDIDATE_COUNT] = "";
    ctx->english_prefix_length = (uint8_t)prefix_length;
    lv_btnmatrix_set_map(ctx->english_candidates,
                         ctx->english_candidate_map);
    lv_obj_clear_flag(ctx->english_candidates, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->english_candidates);
}

static void lvww_english_candidate_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    lv_obj_t *textarea;
    uint16_t button;
    const char *candidate;
    uint8_t i;

    if (!ctx || lv_event_get_code(event) != LV_EVENT_VALUE_CHANGED ||
        ctx->pinyin_enabled || !ctx->keyboard)
        return;
    textarea = lv_keyboard_get_textarea(ctx->keyboard);
    button = lv_btnmatrix_get_selected_btn(lv_event_get_target(event));
    candidate = button == LV_BTNMATRIX_BTN_NONE ? RT_NULL :
                lv_btnmatrix_get_btn_text(lv_event_get_target(event), button);
    if (!textarea || !candidate || !candidate[0])
        return;

    for (i = 0; i < ctx->english_prefix_length; ++i)
        lv_textarea_del_char(textarea);
    lv_textarea_add_text(textarea, candidate);
    lvww_english_candidates_hide(ctx);
}

static void lvww_pinyin_update_mode(lvww_ctx_t *ctx, rt_bool_t enabled)
{
    if (!ctx)
        return;
    lvww_pinyin_reset(ctx);
    lvww_english_candidates_hide(ctx);
    ctx->pinyin_enabled = enabled;
    if (ctx->pinyin_toggle_label)
    {
        lv_label_set_text(ctx->pinyin_toggle_label, enabled ? "中" : "En");
        lv_obj_set_style_text_color(ctx->pinyin_toggle_label,
                                    enabled ? ctx->cfg.accent_color :
                                              lv_color_white(), 0);
    }
    if (ctx->pinyin_toggle)
    {
        lv_obj_set_style_bg_color(ctx->pinyin_toggle,
                                  ctx->cfg.panel_color, 0);
        lv_obj_set_style_bg_opa(ctx->pinyin_toggle, LV_OPA_COVER, 0);
    }
    if (!enabled)
        lvww_english_candidates_update(ctx);
}

static void lvww_pinyin_toggle_cb(lv_event_t *event)
{
    lvww_ctx_t *ctx = (lvww_ctx_t *)lv_event_get_user_data(event);
    if (lv_event_get_code(event) == LV_EVENT_CLICKED && ctx)
        lvww_pinyin_update_mode(ctx, !ctx->pinyin_enabled);
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
    lvww_pinyin_reset(ctx);
    lvww_english_candidates_hide(ctx);
    if (ctx->pinyin_bar)
        lv_obj_add_flag(ctx->pinyin_bar, LV_OBJ_FLAG_HIDDEN);
    if (ctx->pinyin_toggle)
        lv_obj_add_flag(ctx->pinyin_toggle, LV_OBJ_FLAG_HIDDEN);
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
    if (previous != textarea)
        lvww_pinyin_update_mode(ctx, textarea == ctx->city_input);
    if (ctx->pinyin_ime)
        lv_ime_pinyin_set_mode(ctx->pinyin_ime, LV_IME_PINYIN_MODE_K26);
    lv_keyboard_set_mode(ctx->keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(ctx->keyboard, textarea);
    lv_obj_add_state(textarea, LV_STATE_FOCUSED);
    lv_obj_clear_flag(ctx->keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ctx->keyboard);
    if (ctx->pinyin_bar)
    {
        lv_obj_clear_flag(ctx->pinyin_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ctx->pinyin_bar);
    }
    if (ctx->pinyin_toggle)
    {
        lv_obj_clear_flag(ctx->pinyin_toggle, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(ctx->pinyin_toggle);
    }
    if (ctx->pinyin_candidates)
        lv_obj_move_foreground(ctx->pinyin_candidates);
    if (!ctx->pinyin_enabled)
        lvww_english_candidates_update(ctx);
    lv_obj_update_layout(ctx->keyboard);
    lv_obj_invalidate(ctx->keyboard);
    if (!ctx->editor && ctx->city_results)
    {
        /* Fill all space above the keyboard.  The previous fixed 88 px
         * height left a large empty strip and exposed only one city row. */
        keyboard_top = (lv_coord_t)ctx->cfg.height -
                       lv_obj_get_height(ctx->keyboard);
        results_height = keyboard_top - LVWW_PINYIN_BAR_HEIGHT -
                         lv_obj_get_y(ctx->city_results) - 8;
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

    if (code == LV_EVENT_VALUE_CHANGED)
    {
#if LV_USE_IME_PINYIN
        uint16_t button;
        const char *text;
        lv_ime_pinyin_t *ime;

        if (!ctx || !ctx->pinyin_ime)
            return;
        if (!ctx->pinyin_enabled)
        {
            /* The keyboard class has already inserted the key.  Stop only
             * the later IME callback so English input remains unchanged. */
            lvww_english_candidates_update(ctx);
            lv_event_stop_processing(event);
            return;
        }

        button = lv_btnmatrix_get_selected_btn(ctx->keyboard);
        text = button == LV_BTNMATRIX_BTN_NONE ? RT_NULL :
               lv_btnmatrix_get_btn_text(ctx->keyboard, button);
        ime = (lv_ime_pinyin_t *)ctx->pinyin_ime;
        if (text && (rt_strcmp(text, LV_SYMBOL_KEYBOARD) == 0 ||
                     rt_strcmp(text, LV_SYMBOL_CLOSE) == 0))
        {
            /* LVGL also uses this key to switch to K9 inside the IME.  In
             * this UI it is the close key, so keep the configured K26 mode. */
            lvww_pinyin_reset(ctx);
            lv_event_stop_processing(event);
            return;
        }
        if (text && ((text[0] >= 'a' && text[0] <= 'z') ||
                     (text[0] >= 'A' && text[0] <= 'Z')))
        {
            if (ime->ta_count >= LVWW_PINYIN_MAX_SYLLABLE)
            {
                /* A valid Pinyin syllable never exceeds six letters.  Commit
                 * overlong input as Latin text instead of overflowing LVGL's
                 * fixed IME composition buffer. */
                lvww_pinyin_reset(ctx);
                lv_event_stop_processing(event);
            }
            return;
        }
        if (text && rt_strcmp(text, LV_SYMBOL_BACKSPACE) != 0 &&
            rt_strcmp(text, "ABC") != 0 && rt_strcmp(text, "abc") != 0 &&
            rt_strcmp(text, "1#") != 0 &&
            rt_strcmp(text, "Enter") != 0 &&
            rt_strcmp(text, LV_SYMBOL_NEW_LINE) != 0 &&
            rt_strcmp(text, LV_SYMBOL_OK) != 0)
            lvww_pinyin_reset(ctx);
#endif
        return;
    }
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

void lvww_close_editor(lvww_ctx_t *ctx)
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

void lvww_refresh_wifi(lvww_ctx_t *ctx)
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

void lvww_refresh_city_results(lvww_ctx_t *ctx)
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

void lvww_search_timer_cb(lv_timer_t *timer)
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

void lvww_build_ui(lvww_ctx_t *ctx, lv_obj_t *parent)
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
    lv_obj_set_size(ctx->keyboard, ctx->cfg.width, LVWW_KEYBOARD_HEIGHT);
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

#if LV_USE_IME_PINYIN
    ctx->pinyin_bar = lv_obj_create(ctx->root);
    lv_obj_add_flag(ctx->pinyin_bar, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(ctx->pinyin_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(ctx->pinyin_bar, ctx->cfg.width,
                    LVWW_PINYIN_BAR_HEIGHT);
    lv_obj_align_to(ctx->pinyin_bar, ctx->keyboard,
                    LV_ALIGN_OUT_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(ctx->pinyin_bar, ctx->cfg.panel_color, 0);
    lv_obj_set_style_bg_opa(ctx->pinyin_bar, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ctx->pinyin_bar, 0, 0);
    lv_obj_set_style_radius(ctx->pinyin_bar, 0, 0);
    lv_obj_set_style_pad_all(ctx->pinyin_bar, 0, 0);
    lv_obj_add_flag(ctx->pinyin_bar, LV_OBJ_FLAG_HIDDEN);

    ctx->pinyin_ime = lv_ime_pinyin_create(ctx->root);
    lv_obj_set_style_text_font(ctx->pinyin_ime, ctx->cfg.font_city, 0);
    lv_ime_pinyin_set_keyboard(ctx->pinyin_ime, ctx->keyboard);
    lv_ime_pinyin_set_mode(ctx->pinyin_ime, LV_IME_PINYIN_MODE_K26);
    ctx->pinyin_candidates = lv_ime_pinyin_get_cand_panel(ctx->pinyin_ime);
    lv_obj_set_size(ctx->pinyin_candidates,
                    ctx->cfg.width - LVWW_PINYIN_TOGGLE_WIDTH,
                    LVWW_PINYIN_BAR_HEIGHT);
    lv_obj_align_to(ctx->pinyin_candidates, ctx->keyboard,
                    LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ctx->pinyin_candidates, ctx->cfg.panel_color,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->pinyin_candidates, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->pinyin_candidates, 2, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->pinyin_candidates, ctx->cfg.font_city,
                               LV_PART_ITEMS);
    lv_obj_set_style_text_color(ctx->pinyin_candidates, lv_color_white(),
                                LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->pinyin_candidates,
                              lv_color_hex(0x35435B), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(ctx->pinyin_candidates, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->pinyin_candidates, ctx->cfg.accent_color,
                              LV_PART_ITEMS | LV_STATE_PRESSED);

    ctx->english_candidates = lv_btnmatrix_create(ctx->root);
    lv_obj_add_flag(ctx->english_candidates, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(ctx->english_candidates,
                    ctx->cfg.width - LVWW_PINYIN_TOGGLE_WIDTH,
                    LVWW_PINYIN_BAR_HEIGHT);
    lv_obj_align_to(ctx->english_candidates, ctx->keyboard,
                    LV_ALIGN_OUT_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(ctx->english_candidates, ctx->cfg.panel_color,
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(ctx->english_candidates, LV_OPA_COVER,
                            LV_PART_MAIN);
    lv_obj_set_style_border_width(ctx->english_candidates, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(ctx->english_candidates, 2, LV_PART_MAIN);
    lv_obj_set_style_text_font(ctx->english_candidates, ctx->cfg.font_city,
                               LV_PART_ITEMS);
    lv_obj_set_style_text_color(ctx->english_candidates, lv_color_white(),
                                LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->english_candidates,
                              lv_color_hex(0x35435B), LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(ctx->english_candidates, LV_OPA_COVER,
                            LV_PART_ITEMS);
    lv_obj_set_style_bg_color(ctx->english_candidates, ctx->cfg.accent_color,
                              LV_PART_ITEMS | LV_STATE_PRESSED);
    lv_obj_add_event_cb(ctx->english_candidates, lvww_english_candidate_cb,
                        LV_EVENT_VALUE_CHANGED, ctx);
    lv_obj_add_flag(ctx->english_candidates, LV_OBJ_FLAG_HIDDEN);

    ctx->pinyin_toggle = lv_btn_create(ctx->root);
    lv_obj_add_flag(ctx->pinyin_toggle, LV_OBJ_FLAG_FLOATING);
    lv_obj_set_size(ctx->pinyin_toggle, LVWW_PINYIN_TOGGLE_WIDTH,
                    LVWW_PINYIN_BAR_HEIGHT);
    lv_obj_align_to(ctx->pinyin_toggle, ctx->keyboard,
                    LV_ALIGN_OUT_TOP_LEFT, 0, 0);
    lv_obj_set_style_radius(ctx->pinyin_toggle, 0, 0);
    lv_obj_set_style_shadow_width(ctx->pinyin_toggle, 0, 0);
    lv_obj_set_style_bg_color(ctx->pinyin_toggle,
                              lv_color_darken(ctx->cfg.panel_color, 24),
                              LV_STATE_PRESSED);
    ctx->pinyin_toggle_label = lvww_label(ctx, ctx->pinyin_toggle, "中",
                                          ctx->cfg.font_city,
                                          lv_color_white());
    lv_obj_center(ctx->pinyin_toggle_label);
    lv_obj_add_event_cb(ctx->pinyin_toggle, lvww_pinyin_toggle_cb,
                        LV_EVENT_CLICKED, ctx);
    lv_obj_add_flag(ctx->pinyin_toggle, LV_OBJ_FLAG_HIDDEN);
    lvww_pinyin_update_mode(ctx, RT_TRUE);
#endif

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
