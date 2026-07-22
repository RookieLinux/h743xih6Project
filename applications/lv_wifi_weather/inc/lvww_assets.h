#ifndef LVWW_ASSETS_H
#define LVWW_ASSETS_H

#include <rtthread.h>
#include <lvgl.h>
#include "lv_wifi_weather.h"

#ifdef __cplusplus
extern "C" {
#endif

#define LVWW_ASSET_ROOT                 "/ui"
#define LVWW_ASSET_FONT_DIR             LVWW_ASSET_ROOT "/fonts"
#define LVWW_ASSET_IMAGE_DIR            LVWW_ASSET_ROOT "/images"
#define LVWW_ASSET_TEXT_DIR             LVWW_ASSET_ROOT "/text"
#define LVWW_ASSET_DATA_DIR             LVWW_ASSET_ROOT "/data"
#define LVWW_ASSET_FONT_PATH            LVWW_ASSET_FONT_DIR "/lvww_source_han_sans_sc_16_4bpp.fnt"
#define LVWW_ASSET_CITY_DB_PATH         LVWW_ASSET_DATA_DIR "/cities_zh.tsv"

#define LVWW_ASSET_LVGL_ROOT            "Q:/ui"
#define LVWW_ASSET_IMAGE_PATH(name)     LVWW_ASSET_LVGL_ROOT "/images/" name

/* Register Q: with LVGL and load the streaming 4 bpp font from QSPI. */
int lvww_assets_init(void);
int lvww_assets_reload(void);

/* Returns NULL when the external font is not present or is invalid. */
const lv_font_t *lvww_assets_font(void);

/* Search the UTF-8 city database stored in the QSPI filesystem. */
rt_size_t lvww_assets_city_search(const char *query,
                                  lvww_city_t *cities,
                                  rt_size_t capacity);

#ifdef __cplusplus
}
#endif

#endif /* LVWW_ASSETS_H */
