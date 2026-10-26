#include "audio_asf_internal.h"

#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>



static const uint8_t asf_header_guid[ASF_GUID_SIZE] = {
    0x30, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
    0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};
static const uint8_t asf_file_properties_guid[ASF_GUID_SIZE] = {
    0xa1, 0xdc, 0xab, 0x8c, 0x47, 0xa9, 0xcf, 0x11,
    0x8e, 0xe4, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65
};
static const uint8_t asf_stream_properties_guid[ASF_GUID_SIZE] = {
    0x91, 0x07, 0xdc, 0xb7, 0xb7, 0xa9, 0xcf, 0x11,
    0x8e, 0xe6, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65
};
static const uint8_t asf_audio_stream_guid[ASF_GUID_SIZE] = {
    0x40, 0x9e, 0x69, 0xf8, 0x4d, 0x5b, 0xcf, 0x11,
    0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b
};
static const uint8_t asf_audio_conceal_none_guid[ASF_GUID_SIZE] = {
    0x00, 0x57, 0xfb, 0x20, 0x55, 0x5b, 0xcf, 0x11,
    0xa8, 0xfd, 0x00, 0x80, 0x5f, 0x5c, 0x44, 0x2b
};
static const uint8_t asf_audio_conceal_spread_guid[ASF_GUID_SIZE] = {
    0x50, 0xcd, 0xc3, 0xbf, 0x8f, 0x61, 0xcf, 0x11,
    0x8b, 0xb2, 0x00, 0xaa, 0x00, 0xb4, 0xe2, 0x20
};
static const uint8_t asf_data_object_guid[ASF_GUID_SIZE] = {
    0x36, 0x26, 0xb2, 0x75, 0x8e, 0x66, 0xcf, 0x11,
    0xa6, 0xd9, 0x00, 0xaa, 0x00, 0x62, 0xce, 0x6c
};
static const uint8_t asf_header_extension_guid[ASF_GUID_SIZE] = {
    0xb5, 0x03, 0xbf, 0x5f, 0x2e, 0xa9, 0xcf, 0x11,
    0x8e, 0xe3, 0x00, 0xc0, 0x0c, 0x20, 0x53, 0x65
};
static const uint8_t asf_content_encryption_guid[ASF_GUID_SIZE] = {
    0xfb, 0xb3, 0x11, 0x22, 0x23, 0xbd, 0xd2, 0x11,
    0xb4, 0xb7, 0x00, 0xa0, 0xc9, 0x55, 0xfc, 0x6e
};
static const uint8_t asf_extended_content_encryption_guid[ASF_GUID_SIZE] = {
    0x14, 0xe6, 0x8a, 0x29, 0x22, 0x26, 0x17, 0x4c,
    0xb9, 0x35, 0xda, 0xe0, 0x7e, 0xe9, 0x28, 0x9c
};

typedef struct AsfSelectedStream {
    uint16_t format_tag;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    const uint8_t *extradata;
    size_t extradata_size;
    uint16_t descramble_packet_size;
    uint16_t descramble_chunk_size;
    uint8_t stream_number;
    uint8_t descramble_span;
    int found;
    int supported_tag;
} AsfSelectedStream;

static int asf_is_supported_wma_tag(uint16_t format_tag)
{
    return format_tag == 0x000au ||
           (format_tag >= 0x0160u && format_tag <= 0x0163u) ||
           format_tag == 0x0165u || format_tag == 0x0166u;
}

static int asf_parse_file_properties(const uint8_t *data,
                                    size_t object_start,
                                    size_t object_end,
                                    uint32_t *minimum_packet_size,
                                    uint32_t *maximum_packet_size,
                                    uint32_t *preroll_ms,
                                    uint64_t *packet_count,
                                    uint64_t *play_time)
{
    size_t body = object_start + ASF_OBJECT_HEADER_SIZE;
    if (object_end - body < 80u) return QW_STATUS_ERR_FORMAT;
    *packet_count = asf_read_u64_le(data + body + 32u);
    *play_time = asf_read_u64_le(data + body + 40u);
    *preroll_ms = asf_read_u32_le(data + body + 56u);
    *minimum_packet_size = asf_read_u32_le(data + body + 68u);
    *maximum_packet_size = asf_read_u32_le(data + body + 72u);
    if (!*minimum_packet_size || !*maximum_packet_size ||
        *minimum_packet_size > *maximum_packet_size ||
        *maximum_packet_size >= (1u << 29u)) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

static int asf_parse_stream_properties(const uint8_t *data,
                                      size_t object_start,
                                      size_t object_end,
                                      size_t requested_audio_track,
                                      size_t *audio_stream_count,
                                      AsfSelectedStream *selected)
{
    size_t body = object_start + ASF_OBJECT_HEADER_SIZE;
    size_t type_data;
    size_t remaining;
    uint32_t type_specific_size;
    uint32_t error_correction_size;
    uint16_t stream_flags;
    const uint8_t *error_guid;

    if (object_end - body < ASF_STREAM_PROPERTIES_FIXED_SIZE)
        return QW_STATUS_ERR_FORMAT;
    if (!asf_guid_is(data + body, asf_audio_stream_guid)) return QW_STATUS_OK;

    ++*audio_stream_count;
    if (*audio_stream_count != requested_audio_track) return QW_STATUS_OK;
    if (selected->found) return QW_STATUS_ERR_FORMAT;

    error_guid = data + body + ASF_GUID_SIZE;
    type_specific_size = asf_read_u32_le(data + body + 40u);
    error_correction_size = asf_read_u32_le(data + body + 44u);
    stream_flags = asf_read_u16_le(data + body + 48u);
    if (stream_flags & 0x8000u) return QW_STATUS_ERR_UNSUPPORTED;
    if (type_specific_size < ASF_WAVEFORMATEX_SIZE ||
        type_specific_size > object_end - body - ASF_STREAM_PROPERTIES_FIXED_SIZE ||
        error_correction_size > object_end - body - ASF_STREAM_PROPERTIES_FIXED_SIZE -
                                    type_specific_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (asf_guid_is(error_guid, asf_audio_conceal_none_guid) ||
        !memcmp(error_guid, "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0", 16u)) {
        selected->descramble_span = 0u;
    } else if (asf_guid_is(error_guid, asf_audio_conceal_spread_guid)) {
        const uint8_t *descramble = data + body + ASF_STREAM_PROPERTIES_FIXED_SIZE +
                                    type_specific_size;
        if (error_correction_size < 8u) return QW_STATUS_ERR_UNSUPPORTED;
        selected->descramble_span = descramble[0];
        selected->descramble_packet_size = asf_read_u16_le(descramble + 1u);
        selected->descramble_chunk_size = asf_read_u16_le(descramble + 3u);
        if (selected->descramble_span > 1u &&
            (!selected->descramble_packet_size ||
             !selected->descramble_chunk_size ||
             selected->descramble_packet_size % selected->descramble_chunk_size ||
             selected->descramble_packet_size / selected->descramble_chunk_size <= 1u)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    type_data = body + ASF_STREAM_PROPERTIES_FIXED_SIZE;
    remaining = type_specific_size;
    selected->format_tag = asf_read_u16_le(data + type_data);
    selected->channels = asf_read_u16_le(data + type_data + 2u);
    selected->sample_rate = asf_read_u32_le(data + type_data + 4u);
    selected->average_bytes_per_second = asf_read_u32_le(data + type_data + 8u);
    selected->block_align = asf_read_u16_le(data + type_data + 12u);
    selected->bits_per_sample = asf_read_u16_le(data + type_data + 14u);
    if (remaining < ASF_WAVEFORMATEX_SIZE) return QW_STATUS_ERR_FORMAT;
    selected->extradata_size = asf_read_u16_le(data + type_data + 16u);
    if (selected->extradata_size > ASF_MAX_EXTRA_DATA_SIZE ||
        selected->extradata_size > remaining - ASF_WAVEFORMATEX_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    selected->extradata = data + type_data + ASF_WAVEFORMATEX_SIZE;
    selected->stream_number = (uint8_t)(stream_flags & 0x7fu);
    if (!selected->stream_number || !selected->channels ||
        selected->channels > 32u || !selected->sample_rate) {
        return QW_STATUS_ERR_FORMAT;
    }
    selected->supported_tag = asf_is_supported_wma_tag(selected->format_tag);
    selected->found = 1;
    return QW_STATUS_OK;
}

static int asf_header_extension_has_encryption(const uint8_t *data,
                                               size_t object_start,
                                               size_t object_end)
{
    size_t body = object_start + ASF_OBJECT_HEADER_SIZE;
    size_t nested;
    uint32_t extension_size;
    if (object_end - body < 22u) return QW_STATUS_ERR_FORMAT;
    extension_size = asf_read_u32_le(data + body + 18u);
    nested = body + 22u;
    if (extension_size > object_end - nested) return QW_STATUS_ERR_FORMAT;
    object_end = nested + extension_size;
    while (nested < object_end) {
        uint64_t child_size;
        size_t child_end;
        if (!asf_range_is_valid(nested, ASF_OBJECT_HEADER_SIZE, object_end))
            return QW_STATUS_ERR_FORMAT;
        child_size = asf_read_u64_le(data + nested + ASF_GUID_SIZE);
        if (child_size < ASF_OBJECT_HEADER_SIZE ||
            child_size > (uint64_t)(object_end - nested)) return QW_STATUS_ERR_FORMAT;
        child_end = nested + (size_t)child_size;
        if (asf_guid_is(data + nested, asf_content_encryption_guid) ||
            asf_guid_is(data + nested, asf_extended_content_encryption_guid)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        nested = child_end;
    }
    return nested == object_end ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int asf_parse_data_object(QwAudioAsfSource *source,
                                 size_t object_start,
                                 size_t object_end,
                                 uint64_t declared_packet_count,
                                 uint64_t file_packet_count)
{
    uint64_t declared_size = asf_read_u64_le(source->data + object_start + 16u);

    if (object_end < object_start ||
        object_end - object_start < ASF_DATA_OBJECT_HEADER_SIZE)
        return QW_STATUS_ERR_FORMAT;
    if (declared_size != UINT64_MAX &&
        declared_size != (uint64_t)(object_end - object_start)) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->data_end = object_end;
    source->packet_position = object_start + ASF_DATA_OBJECT_HEADER_SIZE;
    source->data_packet_count = declared_packet_count
        ? declared_packet_count : file_packet_count;
    return QW_STATUS_OK;
}

static int asf_parse_header(const uint8_t *data,
                            size_t size,
                            size_t requested_audio_track,
                            QwAudioAsfSource *source)
{
    size_t header_end;
    size_t position;
    uint64_t header_size;
    uint64_t play_time = 0u;
    uint64_t file_packet_count = 0u;
    uint32_t child_count;
    uint32_t child_index;
    int has_file_properties = 0;
    AsfSelectedStream selected = {0};

    if (size < ASF_HEADER_PREFIX_SIZE || !asf_guid_is(data, asf_header_guid))
        return QW_STATUS_ERR_FORMAT;
    header_size = asf_read_u64_le(data + ASF_GUID_SIZE);
    child_count = asf_read_u32_le(data + 24u);
    if (header_size < ASF_HEADER_PREFIX_SIZE || header_size > (uint64_t)size)
        return QW_STATUS_ERR_FORMAT;
    header_end = (size_t)header_size;
    position = ASF_HEADER_PREFIX_SIZE;
    for (child_index = 0u; child_index < child_count; ++child_index) {
        uint64_t object_size;
        size_t object_end;
        int rc;
        if (!asf_range_is_valid(position, ASF_OBJECT_HEADER_SIZE, header_end))
            return QW_STATUS_ERR_FORMAT;
        object_size = asf_read_u64_le(data + position + ASF_GUID_SIZE);
        if (object_size < ASF_OBJECT_HEADER_SIZE ||
            object_size > (uint64_t)(header_end - position)) return QW_STATUS_ERR_FORMAT;
        object_end = position + (size_t)object_size;
        if (asf_guid_is(data + position, asf_file_properties_guid)) {
            if (has_file_properties) return QW_STATUS_ERR_FORMAT;
            rc = asf_parse_file_properties(data, position, object_end,
                                           &source->minimum_packet_size,
                                           &source->maximum_packet_size,
                                           &source->preroll_ms,
                                           &file_packet_count,
                                           &play_time);
            if (rc != QW_STATUS_OK) return rc;
            has_file_properties = 1;
        } else if (asf_guid_is(data + position, asf_stream_properties_guid)) {
            rc = asf_parse_stream_properties(data, position, object_end,
                                             requested_audio_track,
                                             &source->audio_track_count,
                                             &selected);
            if (rc != QW_STATUS_OK) return rc;
        } else if (asf_guid_is(data + position, asf_header_extension_guid)) {
            rc = asf_header_extension_has_encryption(data, position, object_end);
            if (rc != QW_STATUS_OK) return rc;
        } else if (asf_guid_is(data + position, asf_content_encryption_guid) ||
                   asf_guid_is(data + position,
                               asf_extended_content_encryption_guid)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        position = object_end;
    }
    if (position != header_end || !has_file_properties || !selected.found)
        return requested_audio_track > source->audio_track_count
            ? QW_STATUS_ERR_INVALID_ARGUMENT : QW_STATUS_ERR_FORMAT;
    if (!selected.supported_tag) return QW_STATUS_ERR_UNSUPPORTED;
    if (position > size || size - position < ASF_OBJECT_HEADER_SIZE ||
        !asf_guid_is(data + position, asf_data_object_guid)) {
        return QW_STATUS_ERR_FORMAT;
    }
    {
        uint64_t data_object_size = asf_read_u64_le(data + position + ASF_GUID_SIZE);
        uint64_t data_packet_count;
        size_t object_end = size;
        if (size - position < ASF_DATA_OBJECT_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        data_packet_count = asf_read_u64_le(data + position + 40u);
        if (data_object_size != UINT64_MAX) {
            if (data_object_size < ASF_DATA_OBJECT_HEADER_SIZE ||
                data_object_size > (uint64_t)(size - position)) {
                return QW_STATUS_ERR_FORMAT;
            }
            object_end = position + (size_t)data_object_size;
        } else if (!data_packet_count && !file_packet_count) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        {
            int rc = asf_parse_data_object(source, position, object_end,
                                           data_packet_count, file_packet_count);
            if (rc != QW_STATUS_OK) return rc;
        }
    }
    source->sample_rate = selected.sample_rate;
    source->channels = selected.channels;
    source->format_tag = selected.format_tag;
    source->block_align = selected.block_align;
    source->bits_per_sample = selected.bits_per_sample;
    source->average_bytes_per_second = selected.average_bytes_per_second;
    source->extradata = selected.extradata;
    source->extradata_size = selected.extradata_size;
    source->stream_number = selected.stream_number;
    source->descramble_span = selected.descramble_span;
    source->descramble_packet_size = selected.descramble_packet_size;
    source->descramble_chunk_size = selected.descramble_chunk_size;
    if (play_time > (uint64_t)source->preroll_ms * 10000u) {
        uint64_t duration_100ns = play_time -
                                 (uint64_t)source->preroll_ms * 10000u;
        uint64_t seconds = duration_100ns / UINT64_C(10000000);
        uint64_t remainder = duration_100ns % UINT64_C(10000000);
        if (seconds <= UINT64_MAX / source->sample_rate) {
            uint64_t whole_samples = seconds * source->sample_rate;
            uint64_t fractional_samples = remainder * source->sample_rate /
                                          UINT64_C(10000000);
            if (whole_samples <= UINT64_MAX - fractional_samples) {
                source->sample_count = whole_samples + fractional_samples;
            }
        }
    }
    return QW_STATUS_OK;
}

int qw_audio_asf_parse(const uint8_t *data,
                       size_t size,
                       size_t requested_audio_track,
                       QwAudioAsfSource *source)
{
    int rc;
    if (!data || !size || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    source->data = data;
    if (!requested_audio_track) requested_audio_track = 1u;
    rc = asf_parse_header(data, size, requested_audio_track, source);
    if (rc != QW_STATUS_OK) memset(source, 0, sizeof(*source));
    return rc;
}
