#include "ota.h"

#include <fal.h>
#include <rtthread.h>
#include <string.h>

#include <board.h>

#include "ota_config.h"
#include "ota_hash.h"

#define DBG_TAG "app.ota"
#define DBG_LVL DBG_INFO
#include <rtdbg.h>

typedef struct
{
    const struct fal_partition *download;
    const struct fal_partition *upgrade;
    rt_mutex_t lock;
    ota_status_t status;
    ota_crc32_context_t transfer_crc;
    uint32_t expected_package_crc;
    boot_image_header_t header;
} ota_context_t;

static ota_context_t ota_context;
static uint8_t ota_copy_buffer[OTA_COPY_BUFFER_SIZE];

static uint32_t round_up_sector(uint32_t size)
{
    return (size + OTA_FLASH_SECTOR_SIZE - 1U) &
           ~(OTA_FLASH_SECTOR_SIZE - 1U);
}

static int partition_read_exact(const struct fal_partition *partition,
                                uint32_t offset,
                                void *buffer,
                                size_t length)
{
    int result = fal_partition_read(partition, offset,
                                    (uint8_t *)buffer, length);
    return result == (int)length ? OTA_OK : OTA_ERROR_FLASH;
}

static int partition_write_exact(const struct fal_partition *partition,
                                 uint32_t offset,
                                 const void *buffer,
                                 size_t length)
{
    int result = fal_partition_write(partition, offset,
                                     (const uint8_t *)buffer, length);
    return result == (int)length ? OTA_OK : OTA_ERROR_FLASH;
}

static int digest_equal(const uint8_t *left, const uint8_t *right,
                        size_t length)
{
    uint8_t difference = 0U;
    size_t index;

    for (index = 0U; index < length; ++index)
    {
        difference |= left[index] ^ right[index];
    }
    return difference == 0U;
}

static int header_validate(const boot_image_header_t *header)
{
    boot_image_header_t copy;
    uint32_t expected_crc;

    if ((header->magic != BOOT_IMAGE_MAGIC) ||
        (header->format_version != BOOT_IMAGE_FORMAT_VERSION) ||
        (header->header_size != BOOT_IMAGE_HEADER_SIZE) ||
        ((header->flags & BOOT_IMAGE_FLAG_VALID) == 0U) ||
        (header->hardware_id != OTA_HARDWARE_ID) ||
        (header->payload_offset != OTA_IMAGE_PAYLOAD_OFFSET) ||
        (header->image_size < 8U) ||
        (header->image_size > OTA_IMAGE_MAX_SIZE) ||
        (header->load_address != OTA_APP_BASE) ||
        (header->signature_size > sizeof(header->signature)))
    {
        return OTA_ERROR_HEADER;
    }

    if ((header->signature_algorithm == BOOT_SIGNATURE_NONE) &&
        (header->signature_size != 0U))
    {
        return OTA_ERROR_HEADER;
    }

    copy = *header;
    expected_crc = copy.header_crc32;
    copy.header_crc32 = 0U;
    if (ota_crc32_calculate(&copy, sizeof(copy)) != expected_crc)
    {
        return OTA_ERROR_HEADER;
    }
    return OTA_OK;
}

static int partition_payload_validate(
    const struct fal_partition *partition,
    const boot_image_header_t *header,
    int verify_signature)
{
    ota_crc32_context_t crc;
    ota_sha256_context_t sha;
    uint8_t digest[BOOT_SHA256_DIGEST_SIZE];
    uint32_t position = 0U;

    ota_crc32_init(&crc);
    ota_sha256_init(&sha);
    while (position < header->image_size)
    {
        uint32_t remaining = header->image_size - position;
        uint32_t chunk = remaining < sizeof(ota_copy_buffer)
                             ? remaining
                             : sizeof(ota_copy_buffer);
        int result = partition_read_exact(
            partition, header->payload_offset + position,
            ota_copy_buffer, chunk);
        if (result != OTA_OK)
        {
            return result;
        }
        ota_crc32_update(&crc, ota_copy_buffer, chunk);
        ota_sha256_update(&sha, ota_copy_buffer, chunk);
        position += chunk;
    }

    if (ota_crc32_finish(&crc) != header->image_crc32)
    {
        return OTA_ERROR_IMAGE_CRC;
    }
    ota_sha256_finish(&sha, digest);
    if (!digest_equal(digest, header->image_sha256, sizeof(digest)))
    {
        return OTA_ERROR_IMAGE_SHA256;
    }
    if (verify_signature && !ota_signature_verify(header, digest))
    {
        return OTA_ERROR_SIGNATURE;
    }
    return OTA_OK;
}

static int slot_validate(const struct fal_partition *partition,
                         const boot_image_header_t *expected_header)
{
    boot_image_header_t stored_header;
    int result = partition_read_exact(partition, 0U, &stored_header,
                                      sizeof(stored_header));
    if (result != OTA_OK)
    {
        return result;
    }
    result = header_validate(&stored_header);
    if ((result != OTA_OK) ||
        (memcmp(&stored_header, expected_header, sizeof(stored_header)) != 0))
    {
        return OTA_ERROR_HEADER;
    }
    return partition_payload_validate(partition, &stored_header, 1);
}

static int commit_to_upgrade(const boot_image_header_t *header)
{
    uint32_t required_size = header->payload_offset + header->image_size;
    uint32_t erase_size = round_up_sector(required_size);
    uint32_t position = 0U;
    int result;

    if (fal_partition_erase(ota_context.upgrade, 0U, erase_size) !=
        (int)erase_size)
    {
        return OTA_ERROR_FLASH;
    }

    while (position < header->image_size)
    {
        uint32_t remaining = header->image_size - position;
        uint32_t chunk = remaining < sizeof(ota_copy_buffer)
                             ? remaining
                             : sizeof(ota_copy_buffer);
        result = partition_read_exact(
            ota_context.download, header->payload_offset + position,
            ota_copy_buffer, chunk);
        if (result != OTA_OK)
        {
            return result;
        }
        result = partition_write_exact(
            ota_context.upgrade, header->payload_offset + position,
            ota_copy_buffer, chunk);
        if (result != OTA_OK)
        {
            return result;
        }
        position += chunk;
    }

    /* Verify the complete payload before making the upgrade slot bootable. */
    result = partition_payload_validate(ota_context.upgrade, header, 1);
    if (result != OTA_OK)
    {
        return result;
    }

    result = partition_write_exact(ota_context.upgrade, 0U, header,
                                   sizeof(*header));
    if (result != OTA_OK)
    {
        return result;
    }
    return slot_validate(ota_context.upgrade, header);
}

static void set_error(int error)
{
    ota_context.status.state = OTA_STATE_ERROR;
    ota_context.status.last_error = error;
    LOG_E("OTA failed: %s (%d)", ota_result_string(error), error);
}

int ota_service_init(void)
{
    if (ota_context.lock != RT_NULL)
    {
        return OTA_OK;
    }

    ota_context.lock = rt_mutex_create("ota", RT_IPC_FLAG_FIFO);
    if (ota_context.lock == RT_NULL)
    {
        return OTA_ERROR_NO_MEMORY;
    }

    ota_context.download = fal_partition_find(
        OTA_DOWNLOAD_PARTITION_NAME);
    ota_context.upgrade = fal_partition_find(
        OTA_UPGRADE_PARTITION_NAME);
    if ((ota_context.download == RT_NULL) ||
        (ota_context.upgrade == RT_NULL) ||
        (ota_context.download->len < OTA_SLOT_SIZE) ||
        (ota_context.upgrade->len < OTA_SLOT_SIZE))
    {
        ota_context.status.state = OTA_STATE_ERROR;
        ota_context.status.last_error = OTA_ERROR_PARTITION;
        LOG_E("download/upgrade FAL partition missing or too small");
        return OTA_ERROR_PARTITION;
    }

    ota_context.status.state = OTA_STATE_IDLE;
    ota_context.status.last_error = OTA_OK;
    LOG_I("OTA ready: download=%u KiB, upgrade=%u KiB",
          (unsigned)(ota_context.download->len / 1024U),
          (unsigned)(ota_context.upgrade->len / 1024U));
    return OTA_OK;
}

int ota_begin(const char *source,
              uint32_t package_size,
              uint32_t package_crc32)
{
    uint32_t erase_size;
    int result = ota_service_init();

    if (result != OTA_OK)
    {
        return result;
    }
    if ((package_size < BOOT_IMAGE_HEADER_SIZE) ||
        (package_size > OTA_SLOT_SIZE))
    {
        return OTA_ERROR_SIZE;
    }

    rt_mutex_take(ota_context.lock, RT_WAITING_FOREVER);
    if ((ota_context.status.state == OTA_STATE_RECEIVING) ||
        (ota_context.status.state == OTA_STATE_VALIDATING) ||
        (ota_context.status.state == OTA_STATE_COMMITTING))
    {
        rt_mutex_release(ota_context.lock);
        return OTA_ERROR_STATE;
    }

    memset(&ota_context.header, 0, sizeof(ota_context.header));
    memset(&ota_context.status, 0, sizeof(ota_context.status));
    ota_context.status.state = OTA_STATE_RECEIVING;
    ota_context.status.package_size = package_size;
    if (source != RT_NULL)
    {
        rt_strncpy(ota_context.status.source, source,
                   sizeof(ota_context.status.source) - 1U);
    }
    ota_context.expected_package_crc = package_crc32;
    ota_crc32_init(&ota_context.transfer_crc);

    erase_size = round_up_sector(package_size);
    if (fal_partition_erase(ota_context.download, 0U, erase_size) !=
        (int)erase_size)
    {
        set_error(OTA_ERROR_FLASH);
        rt_mutex_release(ota_context.lock);
        return OTA_ERROR_FLASH;
    }

    LOG_I("receiving %u bytes from %s",
          (unsigned)package_size, ota_context.status.source);
    rt_mutex_release(ota_context.lock);
    return OTA_OK;
}

int ota_write(uint32_t offset, const void *data, size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    size_t consumed = 0U;
    int result = OTA_OK;

    if ((data == RT_NULL) || (length == 0U))
    {
        return OTA_ERROR_ARGUMENT;
    }
    if (ota_context.lock == RT_NULL)
    {
        return OTA_ERROR_STATE;
    }

    rt_mutex_take(ota_context.lock, RT_WAITING_FOREVER);
    if ((ota_context.status.state != OTA_STATE_RECEIVING) ||
        (offset != ota_context.status.received_size) ||
        (offset > ota_context.status.package_size) ||
        (length > ota_context.status.package_size - offset))
    {
        rt_mutex_release(ota_context.lock);
        return OTA_ERROR_STATE;
    }

    ota_crc32_update(&ota_context.transfer_crc, data, length);

    if (offset < BOOT_IMAGE_HEADER_SIZE)
    {
        size_t header_length = BOOT_IMAGE_HEADER_SIZE - offset;
        if (header_length > length)
        {
            header_length = length;
        }
        memcpy((uint8_t *)&ota_context.header + offset,
               bytes, header_length);
        consumed = header_length;
    }

    if (consumed < length)
    {
        result = partition_write_exact(
            ota_context.download, offset + (uint32_t)consumed,
            bytes + consumed, length - consumed);
    }

    if (result == OTA_OK)
    {
        ota_context.status.received_size += (uint32_t)length;
    }
    else
    {
        set_error(result);
    }
    rt_mutex_release(ota_context.lock);
    return result;
}

int ota_finish(void)
{
    uint32_t required_size;
    int result;

    if (ota_context.lock == RT_NULL)
    {
        return OTA_ERROR_STATE;
    }

    rt_mutex_take(ota_context.lock, RT_WAITING_FOREVER);
    if ((ota_context.status.state != OTA_STATE_RECEIVING) ||
        (ota_context.status.received_size !=
         ota_context.status.package_size))
    {
        rt_mutex_release(ota_context.lock);
        return OTA_ERROR_STATE;
    }

    ota_context.status.state = OTA_STATE_VALIDATING;
    if (ota_crc32_finish(&ota_context.transfer_crc) !=
        ota_context.expected_package_crc)
    {
        set_error(OTA_ERROR_TRANSFER_CRC);
        rt_mutex_release(ota_context.lock);
        return OTA_ERROR_TRANSFER_CRC;
    }

    result = header_validate(&ota_context.header);
    required_size = ota_context.header.payload_offset +
                    ota_context.header.image_size;
    if ((result != OTA_OK) ||
        (required_size < ota_context.header.payload_offset) ||
        (required_size > ota_context.status.package_size))
    {
        result = result != OTA_OK ? result : OTA_ERROR_SIZE;
        set_error(result);
        rt_mutex_release(ota_context.lock);
        return result;
    }

    result = partition_payload_validate(ota_context.download,
                                        &ota_context.header, 1);
    if (result != OTA_OK)
    {
        set_error(result);
        rt_mutex_release(ota_context.lock);
        return result;
    }

    /*
     * Commit download header last. An interrupted transfer therefore leaves
     * download non-bootable and distinguishable from a complete package.
     */
    result = partition_write_exact(ota_context.download, 0U,
                                   &ota_context.header,
                                   sizeof(ota_context.header));
    if (result == OTA_OK)
    {
        result = slot_validate(ota_context.download, &ota_context.header);
    }
    if (result != OTA_OK)
    {
        set_error(result);
        rt_mutex_release(ota_context.lock);
        return result;
    }

    ota_context.status.state = OTA_STATE_COMMITTING;
    result = commit_to_upgrade(&ota_context.header);
    if (result != OTA_OK)
    {
        set_error(result);
        rt_mutex_release(ota_context.lock);
        return result;
    }

    ota_context.status.state = OTA_STATE_READY;
    ota_context.status.last_error = OTA_OK;
    ota_context.status.firmware_version =
        ota_context.header.firmware_version;
    LOG_I("firmware 0x%08x committed to upgrade",
          (unsigned)ota_context.header.firmware_version);
    rt_mutex_release(ota_context.lock);
    return OTA_OK;
}

void ota_abort(void)
{
    if (ota_context.lock == RT_NULL)
    {
        return;
    }

    rt_mutex_take(ota_context.lock, RT_WAITING_FOREVER);
    if (ota_context.download != RT_NULL)
    {
        /*
         * Clearing only the commit sector is enough to invalidate download
         * and avoids erasing the whole staging slot on cancellation.
         */
        fal_partition_erase(ota_context.download, 0U,
                            OTA_FLASH_SECTOR_SIZE);
    }
    memset(&ota_context.status, 0, sizeof(ota_context.status));
    ota_context.status.state = OTA_STATE_IDLE;
    rt_mutex_release(ota_context.lock);
}

void ota_get_status(ota_status_t *status)
{
    if (status == RT_NULL)
    {
        return;
    }
    if (ota_context.lock == RT_NULL)
    {
        memset(status, 0, sizeof(*status));
        status->state = OTA_STATE_UNINITIALIZED;
        return;
    }
    rt_mutex_take(ota_context.lock, RT_WAITING_FOREVER);
    *status = ota_context.status;
    rt_mutex_release(ota_context.lock);
}

const char *ota_result_string(int result)
{
    switch (result)
    {
    case OTA_OK: return "ok";
    case OTA_ERROR_ARGUMENT: return "invalid argument";
    case OTA_ERROR_STATE: return "invalid state or offset";
    case OTA_ERROR_PARTITION: return "partition unavailable";
    case OTA_ERROR_SIZE: return "package size invalid";
    case OTA_ERROR_FLASH: return "flash I/O failed";
    case OTA_ERROR_TRANSFER_CRC: return "package transfer CRC mismatch";
    case OTA_ERROR_HEADER: return "image header invalid";
    case OTA_ERROR_IMAGE_CRC: return "payload CRC mismatch";
    case OTA_ERROR_IMAGE_SHA256: return "payload SHA-256 mismatch";
    case OTA_ERROR_SIGNATURE: return "signature rejected";
    case OTA_ERROR_NO_MEMORY: return "out of memory";
    case OTA_ERROR_NETWORK: return "network failed";
    case OTA_ERROR_PROTOCOL: return "protocol error";
    case OTA_ERROR_TIMEOUT: return "timeout";
    case OTA_ERROR_PACKAGE_SHA256: return "package SHA-256 mismatch";
    case OTA_ERROR_UNSUPPORTED: return "feature unsupported";
    case OTA_ERROR_CANCELLED: return "operation cancelled";
    default: return "unknown error";
    }
}

int ota_reboot_to_install(uint32_t delay_ms)
{
    ota_status_t status;

    ota_get_status(&status);
    if (status.state != OTA_STATE_READY)
    {
        return OTA_ERROR_STATE;
    }
    if (delay_ms != 0U)
    {
        rt_thread_mdelay((rt_int32_t)delay_ms);
    }
    rt_hw_cpu_reset();
    return OTA_OK;
}

#if defined(__GNUC__)
__attribute__((weak))
#endif
int ota_signature_verify(
    const boot_image_header_t *header,
    const uint8_t image_digest[BOOT_SHA256_DIGEST_SIZE])
{
    (void)image_digest;
    return (header->signature_algorithm == BOOT_SIGNATURE_NONE) &&
           (header->signature_size == 0U);
}
