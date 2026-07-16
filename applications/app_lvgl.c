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

void lv_user_gui_init() {
//    lv_demo_music();
    lvww_rw007_demo_start(lv_scr_act());
//    lv_demo_stress();
//    lv_demo_benchmark();
}
