/*
 * Copyright (c) 2006-2018, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2018-05-17     armink       the first version
 * 2026-04-29     Rookie
 */

#ifndef FAL_CFG_H_
#define FAL_CFG_H_

#include <rtconfig.h>
#include <board.h>

/* ===================== Flash device Configuration ========================= */
extern const struct fal_flash_dev stm32_onchip_flash;
extern struct fal_flash_dev nor_flash0;

/* flash device table */
#define FAL_FLASH_DEV_TABLE                                          \
{                                                                    \
    &stm32_onchip_flash,                                             \
    &nor_flash0,                                                     \
}
/* ====================== Partition Configuration ========================== */
#ifdef FAL_PART_HAS_TABLE_CFG
/* partition table */
#define FAL_PART_TABLE                                                               \
{                                                                                    \
    /*{FAL_PART_MAGIC_WORD,          "bl",       "onchip_flash",             0,      512*1024, 0},*/ \
    /*{FAL_PART_MAGIC_WORD,         "app",        "onchip_flash",          512*1024,      1536*1024, 0},*/ \
    {FAL_PART_MAGIC_WORD,         "app",       "onchip_flash",                            0,     2048*1024, 0}, \
    {FAL_PART_MAGIC_WORD,     "upgrade",        FAL_USING_NOR_FLASH_DEV_NAME,             0,   1*1024*1024, 0}, \
    {FAL_PART_MAGIC_WORD,     "factory",        FAL_USING_NOR_FLASH_DEV_NAME,   1*1024*1024,   1*1024*1024, 0}, \
    {FAL_PART_MAGIC_WORD,    "download",        FAL_USING_NOR_FLASH_DEV_NAME,   2*1024*1024,   1*1024*1024, 0}, \
    {FAL_PART_MAGIC_WORD,  "filesystem",        FAL_USING_NOR_FLASH_DEV_NAME,   3*1024*1024,   5*1024*1024, 0}, \
}
#endif /* FAL_PART_HAS_TABLE_CFG */

#endif /* FAL_CFG_H_ */
