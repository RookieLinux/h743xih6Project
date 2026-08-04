/*
 * Copyright (c) 2006-2026, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-19     RT-Thread    first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdio.h>

#include "ota/ota_wifi.h"
#if defined(PKG_USING_UMQTT) && defined(PKG_USING_WEBCLIENT) && \
    defined(PKG_USING_CJSON) && defined(RT_USING_SAL) && \
    defined(RT_USING_WIFI)
#include "ota/ota_remote.h"
#endif

#define DBG_TAG "main"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#define LED GET_PIN(B,0)
#define LCD_BL GET_PIN(B, 5)

int main(void)
{
    int count = 1;
    LOG_D("main thread started");

    if (ota_wifi_server_start() != 0)
    {
        LOG_E("failed to start OTA service");
    }
#if defined(PKG_USING_UMQTT) && defined(PKG_USING_WEBCLIENT) && \
    defined(PKG_USING_CJSON) && defined(RT_USING_SAL) && \
    defined(RT_USING_WIFI)
    if (ota_remote_start(RT_NULL) != 0)
    {
        LOG_E("failed to start MQTT/HTTP OTA service");
    }
#endif

    rt_pin_mode(LED, PIN_MODE_OUTPUT);
    rt_pin_write(LED, PIN_LOW);

    rt_pin_mode(LCD_BL, PIN_MODE_OUTPUT);
    rt_pin_write(LCD_BL, 1);
    LOG_D("The backlight has been turned on");

    while (1)
    {
        if (count == 30000)
            count = 0;
        count++;
        rt_pin_write(LED, count%2);
        rt_thread_mdelay(1000);
    }

    return RT_EOK;
}
