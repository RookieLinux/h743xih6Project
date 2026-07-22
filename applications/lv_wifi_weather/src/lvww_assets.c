#include "lvww_assets.h"
#include "lv_wifi_weather.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdint.h>
#include <string.h>

#define DBG_TAG "lvww.assets"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

#define LVWW_FONT_MAGIC              "LVWWFNT1"
#define LVWW_FONT_VERSION            1u
#define LVWW_FONT_HEADER_SIZE        38u
#define LVWW_FONT_PIXEL_SIZE         16u
#define LVWW_FONT_BPP                4u
#define LVWW_FONT_GLYPH_BYTES        128u
#define LVWW_FONT_CACHE_COUNT        8u
#define LVWW_SEEK_SET                0
#define LVWW_SEEK_CUR                1
#define LVWW_SEEK_END                2

typedef struct
{
    uint32_t codepoint;
    uint32_t age;
    uint8_t valid;
    uint8_t bitmap[LVWW_FONT_GLYPH_BYTES];
} lvww_glyph_cache_t;

typedef struct
{
    lv_font_t font;
    int fd;
    uint32_t glyph_count;
    uint32_t bitmap_offset;
    uint32_t *codepoints;
    uint32_t cache_clock;
    int16_t glyph_ofs_y;
    lvww_glyph_cache_t cache[LVWW_FONT_CACHE_COUNT];
} lvww_file_font_t;

static lvww_file_font_t file_font;
static rt_bool_t file_font_loaded;
static rt_bool_t file_font_prepared;
static lv_fs_drv_t qspi_fs_drv;
static rt_bool_t qspi_fs_registered;

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static int16_t read_i16_le(const uint8_t *p)
{
    return (int16_t)read_u16_le(p);
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int read_exact_at(int fd, uint32_t offset, void *buffer, uint32_t length)
{
    uint8_t *cursor = (uint8_t *)buffer;
    uint32_t done = 0;

    if (lseek(fd, (off_t)offset, LVWW_SEEK_SET) < 0)
        return -RT_ERROR;

    while (done < length)
    {
        int count = read(fd, cursor + done, length - done);
        if (count <= 0)
            return -RT_ERROR;
        done += (uint32_t)count;
    }
    return RT_EOK;
}

static int find_codepoint(const lvww_file_font_t *state, uint32_t codepoint)
{
    int low = 0;
    int high = (int)state->glyph_count - 1;

    while (low <= high)
    {
        int middle = low + (high - low) / 2;
        uint32_t current = state->codepoints[middle];
        if (current == codepoint)
            return middle;
        if (current < codepoint)
            low = middle + 1;
        else
            high = middle - 1;
    }
    return -1;
}

static bool file_font_get_glyph_dsc(const lv_font_t *font,
                                    lv_font_glyph_dsc_t *dsc,
                                    uint32_t letter,
                                    uint32_t letter_next)
{
    const lvww_file_font_t *state = (const lvww_file_font_t *)font->dsc;
    (void)letter_next;

    if (!state || find_codepoint(state, letter) < 0)
        return false;

    dsc->adv_w = LVWW_FONT_PIXEL_SIZE;
    dsc->box_w = LVWW_FONT_PIXEL_SIZE;
    dsc->box_h = LVWW_FONT_PIXEL_SIZE;
    dsc->ofs_x = 0;
    dsc->ofs_y = state->glyph_ofs_y;
    dsc->bpp = LVWW_FONT_BPP;
    dsc->is_placeholder = 0;
    return true;
}

static const uint8_t *file_font_get_bitmap(const lv_font_t *font, uint32_t letter)
{
    lvww_file_font_t *state = (lvww_file_font_t *)font->dsc;
    lvww_glyph_cache_t *victim = RT_NULL;
    uint32_t oldest_age = UINT32_MAX;
    int index;
    uint32_t i;

    if (!state)
        return RT_NULL;

    index = find_codepoint(state, letter);
    if (index < 0)
        return RT_NULL;

    for (i = 0; i < LVWW_FONT_CACHE_COUNT; ++i)
    {
        lvww_glyph_cache_t *entry = &state->cache[i];
        if (entry->valid && entry->codepoint == letter)
        {
            entry->age = ++state->cache_clock;
            return entry->bitmap;
        }
        if (!entry->valid)
            victim = entry;
        else if (!victim && entry->age < oldest_age)
        {
            oldest_age = entry->age;
            victim = entry;
        }
    }

    if (!victim || read_exact_at(state->fd,
                                  state->bitmap_offset +
                                  (uint32_t)index * LVWW_FONT_GLYPH_BYTES,
                                  victim->bitmap,
                                  LVWW_FONT_GLYPH_BYTES) != RT_EOK)
        return RT_NULL;

    victim->codepoint = letter;
    victim->age = ++state->cache_clock;
    victim->valid = 1;
    return victim->bitmap;
}

static void file_font_prepare(void)
{
    if (file_font_prepared)
        return;

    file_font.fd = -1;
    file_font.glyph_ofs_y = -2;
    file_font.font.get_glyph_dsc = file_font_get_glyph_dsc;
    file_font.font.get_glyph_bitmap = file_font_get_bitmap;
    file_font.font.line_height = 19;
    file_font.font.base_line = 3;
    file_font.font.subpx = LV_FONT_SUBPX_NONE;
    file_font.font.underline_position = -2;
    file_font.font.underline_thickness = 1;
    file_font.font.dsc = &file_font;
    file_font.font.fallback = &lvww_font_cjk_16;
    file_font_prepared = RT_TRUE;
}

static int file_font_open(const char *path)
{
    uint8_t header[LVWW_FONT_HEADER_SIZE];
    uint32_t codepoint_offset;
    uint32_t glyph_count;
    uint32_t table_bytes;
    uint32_t *new_codepoints;
    uint32_t *old_codepoints;
    uint32_t i;
    int old_fd;
    int fd;

    fd = open(path, O_RDONLY, 0);
    if (fd < 0)
        return -RT_ERROR;

    if (read_exact_at(fd, 0, header, sizeof(header)) != RT_EOK ||
        rt_memcmp(header, LVWW_FONT_MAGIC, 8) != 0 ||
        read_u16_le(header + 8) != LVWW_FONT_VERSION ||
        read_u16_le(header + 10) != LVWW_FONT_HEADER_SIZE ||
        read_u16_le(header + 12) != LVWW_FONT_PIXEL_SIZE ||
        header[20] != LVWW_FONT_BPP ||
        read_u16_le(header + 34) != LVWW_FONT_GLYPH_BYTES)
    {
        close(fd);
        return -RT_EINVAL;
    }

    glyph_count = read_u32_le(header + 22);
    codepoint_offset = read_u32_le(header + 26);
    if (!glyph_count || glyph_count > 30000u || glyph_count > UINT32_MAX / 4u)
    {
        close(fd);
        return -RT_EINVAL;
    }

    table_bytes = glyph_count * sizeof(uint32_t);
    new_codepoints = (uint32_t *)rt_malloc(table_bytes);
    if (!new_codepoints)
    {
        close(fd);
        return -RT_ENOMEM;
    }
    if (read_exact_at(fd, codepoint_offset, new_codepoints, table_bytes) != RT_EOK)
    {
        rt_free(new_codepoints);
        close(fd);
        return -RT_ERROR;
    }

    for (i = 1; i < glyph_count; ++i)
    {
        if (new_codepoints[i - 1] >= new_codepoints[i])
        {
            rt_free(new_codepoints);
            close(fd);
            return -RT_EINVAL;
        }
    }

    old_fd = file_font_loaded ? file_font.fd : -1;
    old_codepoints = file_font.codepoints;
    rt_memset(file_font.cache, 0, sizeof(file_font.cache));
    file_font.cache_clock = 0;
    file_font.fd = fd;
    file_font.codepoints = new_codepoints;
    file_font.glyph_count = glyph_count;
    file_font.bitmap_offset = read_u32_le(header + 30);
    file_font.glyph_ofs_y = read_i16_le(header + 18);
    file_font.font.get_glyph_dsc = file_font_get_glyph_dsc;
    file_font.font.get_glyph_bitmap = file_font_get_bitmap;
    file_font.font.line_height = read_u16_le(header + 14);
    file_font.font.base_line = read_i16_le(header + 16);
    file_font.font.subpx = LV_FONT_SUBPX_NONE;
    file_font.font.underline_position = -2;
    file_font.font.underline_thickness = 1;
    file_font.font.dsc = &file_font;
    file_font.font.fallback = &lvww_font_cjk_16;
    file_font_loaded = RT_TRUE;
    if (old_fd >= 0)
        close(old_fd);
    if (old_codepoints)
        rt_free(old_codepoints);
    return RT_EOK;
}

static int handle_to_fd(void *file_p)
{
    return (int)(lv_uintptr_t)file_p - 1;
}

static void make_absolute_path(char *buffer, rt_size_t size, const char *path)
{
    if (path && path[0] == '/')
        rt_snprintf(buffer, size, "%s", path);
    else
        rt_snprintf(buffer, size, "/%s", path ? path : "");
}

static bool qspi_ready(lv_fs_drv_t *drv)
{
    struct stat st;
    (void)drv;
    return stat(LVWW_ASSET_ROOT, &st) == 0;
}

static void *qspi_open(lv_fs_drv_t *drv, const char *path, lv_fs_mode_t mode)
{
    char full_path[LV_FS_MAX_PATH_LENGTH];
    int flags;
    int fd;
    (void)drv;

    if (mode == LV_FS_MODE_RD)
        flags = O_RDONLY;
    else if (mode == LV_FS_MODE_WR)
        flags = O_WRONLY | O_CREAT | O_TRUNC;
    else
        flags = O_RDWR | O_CREAT;

    make_absolute_path(full_path, sizeof(full_path), path);
    fd = open(full_path, flags, 0666);
    return fd < 0 ? RT_NULL : (void *)(lv_uintptr_t)(fd + 1);
}

static lv_fs_res_t qspi_close(lv_fs_drv_t *drv, void *file_p)
{
    (void)drv;
    return close(handle_to_fd(file_p)) == 0 ? LV_FS_RES_OK : LV_FS_RES_FS_ERR;
}

static lv_fs_res_t qspi_read(lv_fs_drv_t *drv, void *file_p, void *buffer,
                             uint32_t bytes_to_read, uint32_t *bytes_read)
{
    int count;
    (void)drv;
    count = read(handle_to_fd(file_p), buffer, bytes_to_read);
    if (count < 0)
        return LV_FS_RES_FS_ERR;
    *bytes_read = (uint32_t)count;
    return LV_FS_RES_OK;
}

static lv_fs_res_t qspi_write(lv_fs_drv_t *drv, void *file_p, const void *buffer,
                              uint32_t bytes_to_write, uint32_t *bytes_written)
{
    int count;
    (void)drv;
    count = write(handle_to_fd(file_p), buffer, bytes_to_write);
    if (count < 0)
        return LV_FS_RES_FS_ERR;
    *bytes_written = (uint32_t)count;
    return LV_FS_RES_OK;
}

static lv_fs_res_t qspi_seek(lv_fs_drv_t *drv, void *file_p, uint32_t position,
                             lv_fs_whence_t whence)
{
    int origin = LVWW_SEEK_SET;
    (void)drv;
    if (whence == LV_FS_SEEK_CUR)
        origin = LVWW_SEEK_CUR;
    else if (whence == LV_FS_SEEK_END)
        origin = LVWW_SEEK_END;
    return lseek(handle_to_fd(file_p), (off_t)position, origin) < 0 ?
           LV_FS_RES_FS_ERR : LV_FS_RES_OK;
}

static lv_fs_res_t qspi_tell(lv_fs_drv_t *drv, void *file_p, uint32_t *position)
{
    off_t current;
    (void)drv;
    current = lseek(handle_to_fd(file_p), 0, LVWW_SEEK_CUR);
    if (current < 0)
        return LV_FS_RES_FS_ERR;
    *position = (uint32_t)current;
    return LV_FS_RES_OK;
}

static void register_qspi_fs(void)
{
    if (qspi_fs_registered)
        return;

    lv_fs_drv_init(&qspi_fs_drv);
    qspi_fs_drv.letter = 'Q';
    qspi_fs_drv.ready_cb = qspi_ready;
    qspi_fs_drv.open_cb = qspi_open;
    qspi_fs_drv.close_cb = qspi_close;
    qspi_fs_drv.read_cb = qspi_read;
    qspi_fs_drv.write_cb = qspi_write;
    qspi_fs_drv.seek_cb = qspi_seek;
    qspi_fs_drv.tell_cb = qspi_tell;
    lv_fs_drv_register(&qspi_fs_drv);
    qspi_fs_registered = RT_TRUE;
}

static void ensure_asset_directories(void)
{
    mkdir(LVWW_ASSET_ROOT, 0777);
    mkdir(LVWW_ASSET_FONT_DIR, 0777);
    mkdir(LVWW_ASSET_IMAGE_DIR, 0777);
    mkdir(LVWW_ASSET_TEXT_DIR, 0777);
}

int lvww_assets_reload(void)
{
    int result;
    file_font_prepare();
    register_qspi_fs();
    ensure_asset_directories();
    result = file_font_open(LVWW_ASSET_FONT_PATH);
    if (result == RT_EOK)
        LOG_I("loaded %u glyphs from %s", file_font.glyph_count, LVWW_ASSET_FONT_PATH);
    else
        LOG_W("font unavailable (%d): %s", result, LVWW_ASSET_FONT_PATH);
    return result;
}

int lvww_assets_init(void)
{
    file_font_prepare();
    if (file_font_loaded)
        return RT_EOK;
    return lvww_assets_reload();
}

const lv_font_t *lvww_assets_font(void)
{
    file_font_prepare();
    return &file_font.font;
}

#ifdef RT_USING_FINSH
static int lvww_assets_cmd(int argc, char **argv)
{
    int result;
    (void)argc;
    (void)argv;
    result = lvww_assets_reload();
    rt_kprintf("QSPI UI font: %s, path=%s\n",
               result == RT_EOK ? "ready" : "missing/invalid",
               LVWW_ASSET_FONT_PATH);
    return result;
}
MSH_CMD_EXPORT_ALIAS(lvww_assets_cmd, uires_reload,
                     reload UI resources from QSPI filesystem);
#endif
