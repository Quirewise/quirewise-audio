#ifndef QUIREWISE_AUDIO_ASF_INTERNAL_H
#define QUIREWISE_AUDIO_ASF_INTERNAL_H

#include "audio_asf_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    ASF_GUID_SIZE = 16u,
    ASF_OBJECT_HEADER_SIZE = 24u,
    ASF_HEADER_PREFIX_SIZE = 30u,
    ASF_DATA_OBJECT_HEADER_SIZE = 50u,
    ASF_STREAM_PROPERTIES_FIXED_SIZE = 54u,
    ASF_WAVEFORMATEX_SIZE = 18u,
    ASF_MAX_MEDIA_OBJECT_SIZE = 1u << 24,
    ASF_MAX_EXTRA_DATA_SIZE = 1u << 16
};

static inline uint16_t asf_read_u16_le(const uint8_t *bytes)
{
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static inline uint32_t asf_read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static inline uint64_t asf_read_u64_le(const uint8_t *bytes)
{
    return (uint64_t)asf_read_u32_le(bytes) |
           ((uint64_t)asf_read_u32_le(bytes + 4u) << 32u);
}

static inline int asf_guid_is(const uint8_t *bytes,
                              const uint8_t guid[ASF_GUID_SIZE])
{
    return bytes && memcmp(bytes, guid, ASF_GUID_SIZE) == 0;
}

static inline int asf_range_is_valid(size_t start, size_t length, size_t end)
{
    return start <= end && length <= end - start;
}

static inline int asf_read_field(const uint8_t *data,
                                 size_t end,
                                 size_t *position,
                                 unsigned type,
                                 uint32_t default_value,
                                 uint32_t *value)
{
    unsigned width = type == 1u ? 1u : type == 2u ? 2u :
                     type == 3u ? 4u : 0u;
    if (!data || !position || !value || *position > end)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!width) {
        *value = default_value;
        return QW_STATUS_OK;
    }
    if (!asf_range_is_valid(*position, width, end)) return QW_STATUS_ERR_FORMAT;
    if (width == 1u) {
        *value = data[*position];
    } else if (width == 2u) {
        *value = asf_read_u16_le(data + *position);
    } else {
        *value = asf_read_u32_le(data + *position);
    }
    *position += width;
    return QW_STATUS_OK;
}

#endif
