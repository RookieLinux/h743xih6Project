/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2026-04-19     Rookie       v1.0.0
 */
#include <drv_qspi.h>
#include <dfs_fs.h>
#include <rtdevice.h>
#include <spi_flash_sfud.h>


//#define DBG_TAG "qspi.dfs"
#define DBG_LVL DBG_LOG
#include <rtdbg.h>

#include <fal.h>
extern int fal_init(void);
INIT_COMPONENT_EXPORT(fal_init);

#define QSPI_CS_PIN      GET_PIN(B, 6)  //片选
static int rt_spi_w25q64_init(void)
{
    /* spi1总线注册设备spi10 */
    stm32_qspi_bus_attach_device("qspi1", "qspi10", QSPI_CS_PIN, 4, RT_NULL, RT_NULL);
    /* spi10设备注册成spi_flash_device */
    if (RT_NULL == rt_sfud_flash_probe(FAL_USING_NOR_FLASH_DEV_NAME, "qspi10"))
    {
        return -RT_ERROR;
    };
    return RT_EOK;
}
INIT_DEVICE_EXPORT(rt_spi_w25q64_init);

#define FS_PARTITION_NAME  "filesystem"

static int fal_elmfat_init(void)
{
    struct fal_blk_device *blk_dev;

    /* create block device */
    blk_dev = (struct fal_blk_device *)fal_blk_device_create(FS_PARTITION_NAME);
    if(blk_dev == RT_NULL)
        LOG_D("Can't create a block device on '%s' partition.", FS_PARTITION_NAME);
    else
        LOG_D("Create a block device on the %s partition of flash successful.", FS_PARTITION_NAME);

    /* mount elmfat file system to FS_PARTITION_NAME */
    if(dfs_mount(FS_PARTITION_NAME, "/", "elm", 0, 0) == 0)
        LOG_D("elmfat filesystem mount success.");
    else {
        /* make a elmfat format filesystem */
        if(dfs_mkfs("elm", FS_PARTITION_NAME) == 0)
        {
            LOG_D("make elmfat filesystem success.");
            if(dfs_mount(FS_PARTITION_NAME, "/", "elm", 0, 0) == 0)
                LOG_D("elmfat filesystem mount success.");
        }
        else {
            return -RT_ERROR;
        }
    }

    return RT_EOK;
}
INIT_ENV_EXPORT(fal_elmfat_init);

