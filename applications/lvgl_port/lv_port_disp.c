/* lv_port_disp.c */
#include "lv_port_disp.h"
#include "stm32h7xx_hal.h"
#include <rtthread.h>
#include "core_cm7.h"

#define DBG_TAG "lv.disp"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

extern LTDC_HandleTypeDef hltdc;

static lv_disp_drv_t disp_drv;
static lv_disp_draw_buf_t draw_buf;
static rt_sem_t ltdc_swap_sem = RT_NULL;
static rt_thread_t lv_swap_thread = RT_NULL;
static volatile lv_disp_drv_t *ltdc_pending_drv = RT_NULL;

static void disp_init(void);
static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p);
extern void dma2d_fill_color(uint32_t dst_addr, uint32_t color, uint32_t width, uint32_t height);

static void lv_swap_thread_entry(void *parameter)
{
    (void)parameter;

    for (;;)
    {
        rt_sem_take(ltdc_swap_sem, RT_WAITING_FOREVER);

        lv_disp_drv_t *drv = (lv_disp_drv_t *)ltdc_pending_drv;
        if (drv != RT_NULL)
        {
            ltdc_pending_drv = RT_NULL;
            /* Keep LVGL calls in thread context. The VBlank reload is complete here. */
            lv_disp_flush_ready(drv);
        }
    }
}

void LTDC_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_LTDC_IRQHandler(&hltdc);
    rt_interrupt_leave();
}

void LTDC_ER_IRQHandler(void)
{
    rt_interrupt_enter();
    HAL_LTDC_IRQHandler(&hltdc);
    rt_interrupt_leave();
}

uint32_t lv_disp_get_fb_addr(fb_index_t index)
{
    return (index == FB_FRONT) ? LCD_FB0_ADDR : LCD_FB1_ADDR;
}

void lv_port_disp_init(void)
{
    disp_init();

    lv_disp_draw_buf_init(&draw_buf,
                          (void *)LCD_FB0_ADDR,
                          (void *)LCD_FB1_ADDR,
                          LCD_WIDTH * LCD_HEIGHT);

    /* Create completion resources before registering the display driver. */
    ltdc_swap_sem = rt_sem_create("ltdc_swap", 0, RT_IPC_FLAG_FIFO);
    if (ltdc_swap_sem == RT_NULL)
    {
        LOG_E("Failed to create LTDC completion semaphore");
        return;
    }

    lv_swap_thread = rt_thread_create("lv_swap", lv_swap_thread_entry,
                                      RT_NULL, 1024, 20, 10);
    if (lv_swap_thread == RT_NULL)
    {
        LOG_E("Failed to create LTDC completion thread");
        rt_sem_delete(ltdc_swap_sem);
        ltdc_swap_sem = RT_NULL;
        return;
    }
    rt_thread_startup(lv_swap_thread);

    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LCD_WIDTH;
    disp_drv.ver_res = LCD_HEIGHT;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.full_refresh = 1;
    disp_drv.direct_mode = 0;
    disp_drv.antialiasing = 1;
    lv_disp_drv_register(&disp_drv);

    LOG_I("Display init: %dx%d, FB0@0x%08X, FB1@0x%08X",
          LCD_WIDTH, LCD_HEIGHT, LCD_FB0_ADDR, LCD_FB1_ADDR);
}

static void disp_init(void)
{
    dma2d_fill_color(LCD_FB0_ADDR, 0x0000, LCD_WIDTH, LCD_HEIGHT);
    dma2d_fill_color(LCD_FB1_ADDR, 0x0000, LCD_WIDTH, LCD_HEIGHT);
    LOG_I("Frame buffers cleared");
}

static void disp_flush(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    if (area->x1 != 0 || area->y1 != 0 ||
        area->x2 != (LCD_WIDTH - 1) || area->y2 != (LCD_HEIGHT - 1))
    {
        LOG_W("Unexpected partial flush: (%d,%d)-(%d,%d)",
              area->x1, area->y1, area->x2, area->y2);
        lv_disp_flush_ready(drv);
        return;
    }

    /* In full-refresh double-buffer mode color_p is the completed framebuffer. */
    uint32_t fb_addr = (uint32_t)color_p;
    uint32_t fb_size = (uint32_t)LCD_WIDTH * (uint32_t)LCD_HEIGHT * sizeof(lv_color_t);
    uint32_t cache_addr = fb_addr & ~31U;
    uint32_t cache_size = (fb_size + (fb_addr - cache_addr) + 31U) & ~31U;

    SCB_CleanDCache_by_Addr((uint32_t *)cache_addr, (int32_t)cache_size);

    /* Submit the address now; hardware latches it during vertical blanking. */
    ltdc_pending_drv = drv;
    if (HAL_LTDC_SetAddress_NoReload(&hltdc, fb_addr, 0) != HAL_OK ||
        HAL_LTDC_Reload(&hltdc, LTDC_RELOAD_VERTICAL_BLANKING) != HAL_OK)
    {
        ltdc_pending_drv = RT_NULL;
        LOG_E("Failed to schedule LTDC VBlank reload");
        lv_disp_flush_ready(drv);
    }
}

/* Called by HAL only after the vertical-blanking reload has completed. */
void HAL_LTDC_ReloadEventCallback(LTDC_HandleTypeDef *hltdc_handle)
{
    (void)hltdc_handle;

    if (ltdc_pending_drv != RT_NULL && ltdc_swap_sem != RT_NULL)
    {
        rt_sem_release(ltdc_swap_sem);
    }
}
