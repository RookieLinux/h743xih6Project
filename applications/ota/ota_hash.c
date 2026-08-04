#include "ota_hash.h"

#include <string.h>

#define OTA_ROTR32(value, bits) \
    (((value) >> (bits)) | ((value) << (32U - (bits))))

static const uint32_t sha256_round_constants[64] =
{
    0x428A2F98UL, 0x71374491UL, 0xB5C0FBCFUL, 0xE9B5DBA5UL,
    0x3956C25BUL, 0x59F111F1UL, 0x923F82A4UL, 0xAB1C5ED5UL,
    0xD807AA98UL, 0x12835B01UL, 0x243185BEUL, 0x550C7DC3UL,
    0x72BE5D74UL, 0x80DEB1FEUL, 0x9BDC06A7UL, 0xC19BF174UL,
    0xE49B69C1UL, 0xEFBE4786UL, 0x0FC19DC6UL, 0x240CA1CCUL,
    0x2DE92C6FUL, 0x4A7484AAUL, 0x5CB0A9DCUL, 0x76F988DAUL,
    0x983E5152UL, 0xA831C66DUL, 0xB00327C8UL, 0xBF597FC7UL,
    0xC6E00BF3UL, 0xD5A79147UL, 0x06CA6351UL, 0x14292967UL,
    0x27B70A85UL, 0x2E1B2138UL, 0x4D2C6DFCUL, 0x53380D13UL,
    0x650A7354UL, 0x766A0ABBUL, 0x81C2C92EUL, 0x92722C85UL,
    0xA2BFE8A1UL, 0xA81A664BUL, 0xC24B8B70UL, 0xC76C51A3UL,
    0xD192E819UL, 0xD6990624UL, 0xF40E3585UL, 0x106AA070UL,
    0x19A4C116UL, 0x1E376C08UL, 0x2748774CUL, 0x34B0BCB5UL,
    0x391C0CB3UL, 0x4ED8AA4AUL, 0x5B9CCA4FUL, 0x682E6FF3UL,
    0x748F82EEUL, 0x78A5636FUL, 0x84C87814UL, 0x8CC70208UL,
    0x90BEFFFAUL, 0xA4506CEBUL, 0xBEF9A3F7UL, 0xC67178F2UL
};

static uint32_t load_be32(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24U) |
           ((uint32_t)data[1] << 16U) |
           ((uint32_t)data[2] << 8U) |
           (uint32_t)data[3];
}

static void store_be32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24U);
    data[1] = (uint8_t)(value >> 16U);
    data[2] = (uint8_t)(value >> 8U);
    data[3] = (uint8_t)value;
}

static void sha256_transform(ota_sha256_context_t *context,
                             const uint8_t block[64])
{
    uint32_t schedule[64];
    uint32_t a = context->state[0];
    uint32_t b = context->state[1];
    uint32_t c = context->state[2];
    uint32_t d = context->state[3];
    uint32_t e = context->state[4];
    uint32_t f = context->state[5];
    uint32_t g = context->state[6];
    uint32_t h = context->state[7];
    uint32_t index;

    for (index = 0U; index < 16U; ++index)
    {
        schedule[index] = load_be32(&block[index * 4U]);
    }
    for (index = 16U; index < 64U; ++index)
    {
        uint32_t s0 = OTA_ROTR32(schedule[index - 15U], 7U) ^
                      OTA_ROTR32(schedule[index - 15U], 18U) ^
                      (schedule[index - 15U] >> 3U);
        uint32_t s1 = OTA_ROTR32(schedule[index - 2U], 17U) ^
                      OTA_ROTR32(schedule[index - 2U], 19U) ^
                      (schedule[index - 2U] >> 10U);
        schedule[index] = schedule[index - 16U] + s0 +
                          schedule[index - 7U] + s1;
    }

    for (index = 0U; index < 64U; ++index)
    {
        uint32_t s1 = OTA_ROTR32(e, 6U) ^ OTA_ROTR32(e, 11U) ^
                      OTA_ROTR32(e, 25U);
        uint32_t choice = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + choice +
                         sha256_round_constants[index] + schedule[index];
        uint32_t s0 = OTA_ROTR32(a, 2U) ^ OTA_ROTR32(a, 13U) ^
                      OTA_ROTR32(a, 22U);
        uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + majority;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    context->state[0] += a;
    context->state[1] += b;
    context->state[2] += c;
    context->state[3] += d;
    context->state[4] += e;
    context->state[5] += f;
    context->state[6] += g;
    context->state[7] += h;
}

void ota_crc32_init(ota_crc32_context_t *context)
{
    context->value = 0xFFFFFFFFUL;
}

void ota_crc32_update(ota_crc32_context_t *context,
                      const void *data,
                      size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t crc = context->value;

    while (length-- != 0U)
    {
        uint32_t bit;
        crc ^= *bytes++;
        for (bit = 0U; bit < 8U; ++bit)
        {
            uint32_t mask = (uint32_t)-(int32_t)(crc & 1U);
            crc = (crc >> 1U) ^ (0xEDB88320UL & mask);
        }
    }
    context->value = crc;
}

uint32_t ota_crc32_finish(const ota_crc32_context_t *context)
{
    return context->value ^ 0xFFFFFFFFUL;
}

uint32_t ota_crc32_calculate(const void *data, size_t length)
{
    ota_crc32_context_t context;
    ota_crc32_init(&context);
    ota_crc32_update(&context, data, length);
    return ota_crc32_finish(&context);
}

void ota_sha256_init(ota_sha256_context_t *context)
{
    static const uint32_t initial_state[8] =
    {
        0x6A09E667UL, 0xBB67AE85UL, 0x3C6EF372UL, 0xA54FF53AUL,
        0x510E527FUL, 0x9B05688CUL, 0x1F83D9ABUL, 0x5BE0CD19UL
    };

    memcpy(context->state, initial_state, sizeof(initial_state));
    context->bit_length = 0U;
    context->buffer_length = 0U;
}

void ota_sha256_update(ota_sha256_context_t *context,
                       const void *data,
                       size_t length)
{
    const uint8_t *bytes = (const uint8_t *)data;

    context->bit_length += (uint64_t)length * 8U;
    while (length != 0U)
    {
        size_t available = sizeof(context->buffer) - context->buffer_length;
        size_t copy_length = length < available ? length : available;

        memcpy(&context->buffer[context->buffer_length], bytes, copy_length);
        context->buffer_length += (uint32_t)copy_length;
        bytes += copy_length;
        length -= copy_length;

        if (context->buffer_length == sizeof(context->buffer))
        {
            sha256_transform(context, context->buffer);
            context->buffer_length = 0U;
        }
    }
}

void ota_sha256_finish(ota_sha256_context_t *context,
                       uint8_t digest[BOOT_SHA256_DIGEST_SIZE])
{
    uint32_t index = context->buffer_length;
    uint32_t state_index;

    context->buffer[index++] = 0x80U;
    if (index > 56U)
    {
        memset(&context->buffer[index], 0, 64U - index);
        sha256_transform(context, context->buffer);
        index = 0U;
    }
    memset(&context->buffer[index], 0, 56U - index);
    for (state_index = 0U; state_index < 8U; ++state_index)
    {
        context->buffer[63U - state_index] =
            (uint8_t)(context->bit_length >> (state_index * 8U));
    }
    sha256_transform(context, context->buffer);

    for (state_index = 0U; state_index < 8U; ++state_index)
    {
        store_be32(&digest[state_index * 4U], context->state[state_index]);
    }
}
