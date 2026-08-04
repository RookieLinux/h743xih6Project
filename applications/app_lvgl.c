/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-05-06     Rookie       the first version
 */
#include "lv_wifi_weather/inc/lvww_rw007_demo.h"
#include "ota/ota_config.h"
#if defined(PKG_USING_UMQTT) && defined(PKG_USING_WEBCLIENT) && \
    defined(PKG_USING_CJSON) && defined(RT_USING_SAL) && \
    defined(RT_USING_WIFI)
#include "ota/ota_remote.h"
#endif

void lv_user_gui_init() {
    lvww_firmware_info_t firmware;
//    lv_demo_music();
    if (lvww_rw007_demo_start(lv_scr_act()) == RT_EOK) {
        lvww_firmware_info_init(&firmware);
        firmware.current_version_code =
            OTA_CURRENT_FIRMWARE_VERSION_CODE;
        rt_strncpy(firmware.current_version,
                   OTA_CURRENT_FIRMWARE_VERSION_TEXT,
                   sizeof(firmware.current_version) - 1U);
        lvww_set_firmware_info(lvww_rw007_demo_ui(), &firmware);
#if defined(PKG_USING_UMQTT) && defined(PKG_USING_WEBCLIENT) && \
    defined(PKG_USING_CJSON) && defined(RT_USING_SAL) && \
    defined(RT_USING_WIFI)
        ota_remote_bind_ui(lvww_rw007_demo_ui());
#endif
    }
//    lv_demo_stress();
//    lv_demo_benchmark();
}
