#include "lvww_private.h"

#include <stddef.h>

void lvww_secure_zero(void *data, rt_size_t length)
{
    volatile uint8_t *p = (volatile uint8_t *)data;
    while (length--)
        *p++ = 0;
}

static uint32_t lvww_crc32(const void *data, rt_size_t length)
{
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0xFFFFFFFFu;
    rt_size_t i;
    int bit;

    for (i = 0; i < length; ++i)
    {
        crc ^= p[i];
        for (bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u & (uint32_t)-(int32_t)(crc & 1u));
    }
    return ~crc;
}


int lvww_find_profile(lvww_ctx_t *ctx, const char *ssid)
{
    uint8_t i;
    for (i = 0; i < ctx->profile_store.count; ++i)
    {
        if (rt_strncmp(ctx->profile_store.profiles[i].ssid, ssid,
                       LVWW_SSID_MAX_LEN + 1) == 0)
            return i;
    }
    return LVWW_INVALID_INDEX;
}

void lvww_save_profiles(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.kv_write)
        return;
    ctx->profile_store.magic = LVWW_STORE_MAGIC;
    ctx->profile_store.version = LVWW_STORE_VERSION;
    ctx->profile_store.crc = lvww_crc32(&ctx->profile_store,
                                        offsetof(lvww_profile_store_t, crc));
    rc = ctx->ops.kv_write(ctx->user_ctx, LVWW_PROFILE_KEY, &ctx->profile_store,
                           sizeof(ctx->profile_store), LVWW_KV_FLAG_SECRET);
    if (rc != RT_EOK)
        lvww_show_toast(ctx, "Wi-Fi 配置保存失败", RT_TRUE);
}

void lvww_save_cache(lvww_ctx_t *ctx)
{
    int rc;
    if (!ctx->ops.kv_write)
        return;
    ctx->cache_store.magic = LVWW_STORE_MAGIC;
    ctx->cache_store.version = LVWW_STORE_VERSION;
    ctx->cache_store.crc = lvww_crc32(&ctx->cache_store,
                                      offsetof(lvww_cache_store_t, crc));
    rc = ctx->ops.kv_write(ctx->user_ctx, LVWW_CACHE_KEY, &ctx->cache_store,
                           sizeof(ctx->cache_store), 0);
    if (rc != RT_EOK)
        lvww_show_toast(ctx, "天气设置保存失败", RT_TRUE);
}

void lvww_load_store(lvww_ctx_t *ctx)
{
    rt_size_t length;
    uint32_t crc;
    lvww_city_t normalized_city;

    rt_memset(&ctx->profile_store, 0, sizeof(ctx->profile_store));
    ctx->profile_store.last_success = LVWW_INVALID_INDEX;
    if (ctx->ops.kv_read)
    {
        length = sizeof(ctx->profile_store);
        if (ctx->ops.kv_read(ctx->user_ctx, LVWW_PROFILE_KEY,
                             &ctx->profile_store, &length) != RT_EOK ||
            length != sizeof(ctx->profile_store))
        {
            rt_memset(&ctx->profile_store, 0, sizeof(ctx->profile_store));
            ctx->profile_store.last_success = LVWW_INVALID_INDEX;
        }
    }
    crc = lvww_crc32(&ctx->profile_store, offsetof(lvww_profile_store_t, crc));
    if (ctx->profile_store.magic != LVWW_STORE_MAGIC ||
        ctx->profile_store.version != LVWW_STORE_VERSION ||
        ctx->profile_store.count > ctx->cfg.max_profiles ||
        ctx->profile_store.crc != crc)
    {
        lvww_secure_zero(&ctx->profile_store, sizeof(ctx->profile_store));
        ctx->profile_store.last_success = LVWW_INVALID_INDEX;
    }

    rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
    if (ctx->ops.kv_read)
    {
        length = sizeof(ctx->cache_store);
        if (ctx->ops.kv_read(ctx->user_ctx, LVWW_CACHE_KEY,
                             &ctx->cache_store, &length) != RT_EOK ||
            length != sizeof(ctx->cache_store))
            rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
    }
    crc = lvww_crc32(&ctx->cache_store, offsetof(lvww_cache_store_t, crc));
    if (ctx->cache_store.magic != LVWW_STORE_MAGIC ||
        ctx->cache_store.version != LVWW_STORE_VERSION ||
        ctx->cache_store.crc != crc)
    {
        rt_memset(&ctx->cache_store, 0, sizeof(ctx->cache_store));
        ctx->cache_store.city = ctx->cfg.default_city;
        ctx->cache_store.city_valid = ctx->cfg.default_city.id[0] != '\0';
    }
    else if (ctx->cache_store.city_valid &&
             lvww_city_catalog_search(ctx->cache_store.city.id,
                                      &normalized_city, 1) == 1 &&
             rt_strcmp(normalized_city.id, ctx->cache_store.city.id) == 0)
    {
        /* Migrate old English display names without discarding weather data. */
        ctx->cache_store.city = normalized_city;
    }
}


void lvww_upsert_pending_profile(lvww_ctx_t *ctx)
{
    int index;
    if (!ctx->pending_valid)
        return;
    index = lvww_find_profile(ctx, ctx->pending_credentials.ssid);
    if (index < 0)
    {
        if (ctx->profile_store.count >= ctx->cfg.max_profiles)
            return;
        index = ctx->profile_store.count++;
    }
    ctx->profile_store.profiles[index] = ctx->pending_credentials;
    ctx->profile_store.last_success = (int8_t)index;
    lvww_save_profiles(ctx);
    lvww_secure_zero(&ctx->pending_credentials, sizeof(ctx->pending_credentials));
    ctx->pending_valid = RT_FALSE;
}
