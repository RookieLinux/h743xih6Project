/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-28     Rookie       the first version
 */
#include <rtthread.h>
#include <rtdevice.h>
#include <touch.h>

#define DBG_TAG "app_touch"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

/* 显示分辨率 */
#define SCREEN_WIDTH    800
#define SCREEN_HEIGHT   480

/* 触摸面板物理分辨率 */
#define TOUCH_MAX_X     1024
#define TOUCH_MAX_Y     600

/* 触摸数据 */
#define MAX_TOUCH_POINTS 5
static struct rt_touch_data touch_data[MAX_TOUCH_POINTS];

/* 映射后的屏幕坐标 */
typedef struct {
    int x;
    int y;
} screen_point_t;

/**
 * @brief 将触摸坐标映射到屏幕坐标（线性缩放）
 * @param raw GT911 原始触摸数据
 * @param point 输出的屏幕坐标
 */
void touch_to_screen(struct rt_touch_data *raw, screen_point_t *point)
{
    /* 线性映射：触摸坐标 * 屏幕分辨率 / 触摸分辨率 */
    point->x = raw->x_coordinate * SCREEN_WIDTH / TOUCH_MAX_X;
    point->y = raw->y_coordinate * SCREEN_HEIGHT / TOUCH_MAX_Y;

    /* 边界保护（防止计算溢出或异常值） */
    if (point->x >= SCREEN_WIDTH)  point->x = SCREEN_WIDTH - 1;
    if (point->x < 0)              point->x = 0;
    if (point->y >= SCREEN_HEIGHT) point->y = SCREEN_HEIGHT - 1;
    if (point->y < 0)              point->y = 0;
}

void gt911_read_task(void *parameter)
{
    rt_device_t dev = RT_NULL;
    rt_err_t result;

    /* 1. 查找设备 */
    dev = rt_device_find("gt911");
    if (dev == RT_NULL)
    {
        LOG_E("Can't find device: gt911");
        return;
    }

    /* 2. 打开设备（以中断模式或轮询模式） */
    result = rt_device_open(dev, RT_DEVICE_FLAG_INT_RX);//调用init和open
    if (result != RT_EOK)
    {
        LOG_E("Open device failed");
        return;
    }
    rt_thread_mdelay(5);

    struct rt_touch_info info;
    dev->control(dev, RT_TOUCH_CTRL_GET_INFO, &info);
    LOG_I("LCD max x:%d, max y:%d", info.range_x, info.range_y);
//    LOG_D("GT911 device opened, ready to read touch data...");
    /* 3. 循环读取触摸数据 */
    while (1)
    {
        /* 读取触摸点数据，返回实际读取到的触摸点数 */
        rt_size_t read_num = rt_device_read(dev, 0, &touch_data, MAX_TOUCH_POINTS);

        if (read_num > 0)
        {
            screen_point_t screen_pos;
            for (int i = 0; i < read_num; i++)
            {
                /* 解析触摸数据 */
                if (touch_data[i].event == RT_TOUCH_EVENT_DOWN ||
                    touch_data[i].event == RT_TOUCH_EVENT_MOVE)
                {
                    touch_to_screen(&touch_data[i], &screen_pos);
                    rt_kprintf("Touch: x=%d, y=%d, id=%d, event=%d, width=%d\n",
                        screen_pos.x,
                        screen_pos.y,
                        touch_data[i].track_id,
                        touch_data[i].event,
                        touch_data[i].width);
                }
                else if (touch_data[i].event == RT_TOUCH_EVENT_UP)
                {
                    rt_kprintf("Touch UP: id=%d\n", touch_data[i].track_id);
                }
            }
        }

        rt_thread_mdelay(10);  /* 轮询间隔，如使用中断模式可适当调整 */
    }

    /* 清理 */
    rt_device_close(dev);
}

/* 创建读取线程 */
static int gt911_read_init(void)
{
    rt_thread_t tid = rt_thread_create("gt911_read",
                                       gt911_read_task, RT_NULL,
                                       1536, 9, 10);
    if (tid != RT_NULL)
    {
        rt_thread_startup(tid);
    }
    return 0;
}
//INIT_APP_EXPORT(gt911_read_init);
