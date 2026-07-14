/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-05-07     Rookie       the first version
 */
#ifndef APPLICATIONS_LVGL_PORT_LV_PORT_DISP_H_
#define APPLICATIONS_LVGL_PORT_LV_PORT_DISP_H_

#include "lvgl.h"

/* 屏幕参数 - 根据实际硬件修改 */
#define LCD_WIDTH           800
#define LCD_HEIGHT          480
#define LCD_BITS_PER_PIXEL  16      /* RGB565 */
#define LCD_PIXEL_SIZE      (LCD_BITS_PER_PIXEL/8)
#define LCD_BUF_SIZE        (LCD_WIDTH * LCD_HEIGHT * LCD_PIXEL_SIZE)

/* 帧缓冲地址 - 根据 SRAM/SDRAM 布局调整 */
#define LCD_FB0_ADDR        0xC0000000  /* 双缓存: 缓冲0 */
#define LCD_FB1_ADDR        0xC00BB800  /* 双缓存: 缓冲1 (800*480*2 = 768000 bytes) */

/* 双缓冲结构 */
typedef enum {
    FB_FRONT = 0,   /* 当前显示帧 */
    FB_BACK  = 1    /* 绘制帧 */
} fb_index_t;

/* 对外接口 */
void lv_port_disp_init(void);
/* 获取当前缓冲指针 */
uint32_t lv_disp_get_fb_addr(fb_index_t index);
#endif /* APPLICATIONS_LVGL_PORT_LV_PORT_DISP_H_ */
