/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-11-7      SummerGift   first version
 */

#ifndef __DRV_COMMON_H__

#define __DRV_COMMON_H__

#include <rtthread.h>
#include <rthw.h>
#include <board.h>
#include <stm32h7xx.h>

#ifdef __cplusplus
extern "C"
{
#endif

void _Error_Handler(char *s, int num);

#ifndef Error_Handler
#define Error_Handler() _Error_Handler(__FILE__, __LINE__)
#endif

#define DMA_NOT_AVAILABLE ((DMA_INSTANCE_TYPE *)0xFFFFFFFFU)

#define __STM32_PORT(port)  GPIO##port##_BASE
#define GET_PIN(PORTx,PIN) (rt_base_t)((16 * ( ((rt_base_t)__STM32_PORT(PORTx) - (rt_base_t)GPIOA_BASE)/(0x0400UL) )) + PIN)
#define STM32_FLASH_START_ADRESS       ROM_START
#define STM32_FLASH_SIZE               ROM_SIZE
#define STM32_FLASH_END_ADDRESS        ROM_END

#define STM32_SRAM1_SIZE               RAM_SIZE
#define STM32_SRAM1_START              RAM_START
#define STM32_SRAM1_END                RAM_END

#if defined(__CC_ARM) || defined(__CLANG_ARM)
extern int Image$RW_IRAM1$ZI$Limit;
#define HEAP_BEGIN      ((void *)&Image$RW_IRAM1$ZI$Limit)
#define HEAP_END        STM32_SRAM1_END
#elif __ICCARM__
#pragma section="CSTACK"
#define HEAP_BEGIN      (__segment_end("CSTACK"))
#define HEAP_END        STM32_SRAM1_END
#else
extern unsigned char __heap_start;
extern unsigned char __heap_end;
#define HEAP_BEGIN      ((void *)&__heap_start)
#define HEAP_END        ((void *)&__heap_end)
#endif

/* GNU linker section helpers. NOLOAD data must be initialized by the user. */
#if defined(__GNUC__)
#define BSP_SECTION_ITCM_TEXT          __attribute__((section(".itcm_text")))
#define BSP_SECTION_AXI_SRAM           __attribute__((section(".axi_sram")))
#define BSP_SECTION_SRAM1              __attribute__((section(".sram1")))
#define BSP_SECTION_SRAM2              __attribute__((section(".sram2")))
#define BSP_SECTION_SRAM3              __attribute__((section(".sram3")))
#define BSP_SECTION_SRAM4              __attribute__((section(".sram4")))
#define BSP_SECTION_BACKUP_SRAM        __attribute__((section(".backup_sram")))
#define BSP_SECTION_SDRAM              __attribute__((section(".sdram")))
#else
#define BSP_SECTION_ITCM_TEXT
#define BSP_SECTION_AXI_SRAM
#define BSP_SECTION_SRAM1
#define BSP_SECTION_SRAM2
#define BSP_SECTION_SRAM3
#define BSP_SECTION_SRAM4
#define BSP_SECTION_BACKUP_SRAM
#define BSP_SECTION_SDRAM
#endif
#ifdef __cplusplus
}
#endif

#endif
