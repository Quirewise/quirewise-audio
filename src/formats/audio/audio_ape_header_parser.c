#include "audio_ape_header_parser.h"

#include "audio_id3_tag.h"
#include "audio_trailing_tags.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    APE_MIN_VERSION = 3800u,
    APE_MAX_VERSION = 3990u,
    APE_MODERN_VERSION = 3980u,
    APE_DESCRIPTOR_SIZE = 52u,
    APE_MODERN_HEADER_SIZE = 24u,
    APE_LEGACY_HEADER_SIZE = 32u,
    APE_FLAG_8_BIT = 1u,
    APE_FLAG_HAS_PEAK_LEVEL = 4u,
    APE_FLAG_24_BIT = 8u,
    APE_FLAG_HAS_SEEK_ELEMENTS = 16u
};

static int ape_compression_type_supported(uint16_t compression_type)
{
    return compression_type == 1000u || compression_type == 2000u ||
           compression_type == 3000u || compression_type == 4000u ||
           compression_type == 5000u;
}

static int ape_shape_supported(const QwAudioApeSource *source)
{
    return ape_compression_type_supported(source->compression_type) &&
           source->channels >= 1u && source->channels <= 8u &&
           (source->bits_per_sample == 8u ||
            source->bits_per_sample == 16u ||
            source->bits_per_sample == 24u) &&
           source->sample_rate > 0u && source->sample_rate <= 1000000u &&
           source->frame_count > 0u && source->blocks_per_frame > 0u &&
           source->final_frame_blocks > 0u &&
           source->final_frame_blocks <= source->blocks_per_frame;
}

static int ape_sample_count(const QwAudioApeSource *source,
                            uint64_t *sample_count)
{
    uint64_t preceding_frames;

    if (!source || !sample_count) return QW_STATUS_ERR_INVALID_ARGUMENT;
    preceding_frames = (uint64_t)source->frame_count - 1u;
    if (preceding_frames >
        (UINT64_MAX - source->final_frame_blocks) /
            source->blocks_per_frame) {
        return QW_STATUS_ERR_FORMAT;
    }
    *sample_count = preceding_frames * source->blocks_per_frame +
                    source->final_frame_blocks;
    return *sample_count ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int ape_seek_table_validate(size_t size,
                                   size_t seek_table_offset,
                                   uint32_t seek_table_length,
                                   uint32_t frame_count)
{
    if (seek_table_length % sizeof(uint32_t) != 0u ||
        (uint64_t)frame_count * sizeof(uint32_t) > seek_table_length ||
        seek_table_offset > size ||
        seek_table_length > size - seek_table_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

static int ape_parse_modern_header(const uint8_t *bytes,
                                   size_t size,
                                   size_t stream_offset,
                                   QwAudioApeSource *source)
{
    size_t header_offset;
    size_t seek_table_offset;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint32_t descriptor_length;
    uint32_t header_length;
    uint32_t seek_table_length;
    uint32_t wav_header_length;
    uint32_t wav_tail_length;
    uint64_t audio_data_length;
    uint32_t frame_count;
    int rc;

    if (size - stream_offset < APE_DESCRIPTOR_SIZE) return QW_STATUS_ERR_FORMAT;
    descriptor_length = qw_read_u32_le(bytes + stream_offset + 8u);
    header_length = qw_read_u32_le(bytes + stream_offset + 12u);
    seek_table_length = qw_read_u32_le(bytes + stream_offset + 16u);
    wav_header_length = qw_read_u32_le(bytes + stream_offset + 20u);
    audio_data_length = ((uint64_t)qw_read_u32_le(bytes + stream_offset + 28u)
                         << 32u) |
                        qw_read_u32_le(bytes + stream_offset + 24u);
    wav_tail_length = qw_read_u32_le(bytes + stream_offset + 32u);
    if (descriptor_length < APE_DESCRIPTOR_SIZE ||
        descriptor_length > size - stream_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    header_offset = stream_offset + descriptor_length;
    if (header_length < APE_MODERN_HEADER_SIZE ||
        header_length > size - header_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    seek_table_offset = header_offset + header_length;
    rc = ape_seek_table_validate(size, seek_table_offset,
                                 seek_table_length,
                                 qw_read_u32_le(bytes + header_offset + 12u));
    if (rc != QW_STATUS_OK) return rc;
    if (wav_header_length > size - seek_table_offset - seek_table_length)
        return QW_STATUS_ERR_FORMAT;
    first_frame_offset = seek_table_offset + seek_table_length +
                         wav_header_length;
    if (first_frame_offset > size ||
        audio_data_length > (uint64_t)(size - first_frame_offset)) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame_data_end = first_frame_offset + (size_t)audio_data_length;
    if (wav_tail_length > size - frame_data_end) return QW_STATUS_ERR_FORMAT;

    source->version = qw_read_u16_le(bytes + stream_offset + 4u);
    source->compression_type = qw_read_u16_le(bytes + header_offset);
    source->format_flags = qw_read_u16_le(bytes + header_offset + 2u);
    source->blocks_per_frame = qw_read_u32_le(bytes + header_offset + 4u);
    source->final_frame_blocks = qw_read_u32_le(bytes + header_offset + 8u);
    frame_count = qw_read_u32_le(bytes + header_offset + 12u);
    source->bits_per_sample = qw_read_u16_le(bytes + header_offset + 16u);
    source->channels = qw_read_u16_le(bytes + header_offset + 18u);
    source->sample_rate = qw_read_u32_le(bytes + header_offset + 20u);
    if (frame_count == 0u) return QW_STATUS_ERR_FORMAT;
    source->frame_count = frame_count;
    source->seek_table_offset = seek_table_offset;
    source->first_frame_offset = first_frame_offset;
    source->frame_data_end = frame_data_end;
    return QW_STATUS_OK;
}

static int ape_parse_legacy_header(const uint8_t *bytes,
                                   size_t size,
                                   size_t stream_offset,
                                   QwAudioApeSource *source)
{
    size_t header_length = APE_LEGACY_HEADER_SIZE;
    size_t header_end;
    size_t seek_table_offset;
    size_t seek_table_end;
    size_t first_frame_offset;
    size_t trailing_end;
    uint32_t seek_table_length;
    uint32_t wav_header_length;
    uint32_t wav_tail_length;
    uint32_t frame_count;
    uint32_t final_frame_blocks;
    uint32_t seek_element_count;
    uint64_t blocks_per_frame;
    int rc;

    if (size - stream_offset < APE_LEGACY_HEADER_SIZE)
        return QW_STATUS_ERR_FORMAT;
    source->version = qw_read_u16_le(bytes + stream_offset + 4u);
    source->compression_type = qw_read_u16_le(bytes + stream_offset + 6u);
    source->format_flags = qw_read_u16_le(bytes + stream_offset + 8u);
    source->channels = qw_read_u16_le(bytes + stream_offset + 10u);
    source->sample_rate = qw_read_u32_le(bytes + stream_offset + 12u);
    wav_header_length = qw_read_u32_le(bytes + stream_offset + 16u);
    wav_tail_length = qw_read_u32_le(bytes + stream_offset + 20u);
    frame_count = qw_read_u32_le(bytes + stream_offset + 24u);
    final_frame_blocks = qw_read_u32_le(bytes + stream_offset + 28u);

    if (!frame_count || !final_frame_blocks) return QW_STATUS_ERR_FORMAT;
    if (source->format_flags & APE_FLAG_HAS_PEAK_LEVEL) {
        if (header_length > SIZE_MAX - sizeof(uint32_t))
            return QW_STATUS_ERR_FORMAT;
        header_length += sizeof(uint32_t);
    }
    if (source->format_flags & APE_FLAG_HAS_SEEK_ELEMENTS) {
        size_t seek_count_offset;
        if (header_length > SIZE_MAX - sizeof(uint32_t))
            return QW_STATUS_ERR_FORMAT;
        seek_count_offset = stream_offset + header_length;
        if (seek_count_offset > size ||
            sizeof(uint32_t) > size - seek_count_offset) {
            return QW_STATUS_ERR_FORMAT;
        }
        seek_element_count = qw_read_u32_le(bytes + seek_count_offset);
        if (seek_element_count > UINT32_MAX / sizeof(uint32_t))
            return QW_STATUS_ERR_FORMAT;
        seek_table_length = seek_element_count * (uint32_t)sizeof(uint32_t);
        header_length += sizeof(uint32_t);
    } else {
        if (frame_count > UINT32_MAX / sizeof(uint32_t))
            return QW_STATUS_ERR_FORMAT;
        seek_table_length = frame_count * (uint32_t)sizeof(uint32_t);
    }
    header_end = stream_offset + header_length;
    if (header_end > size || wav_header_length > size - header_end)
        return QW_STATUS_ERR_FORMAT;
    seek_table_offset = header_end + wav_header_length;
    rc = ape_seek_table_validate(size, seek_table_offset,
                                 seek_table_length, frame_count);
    if (rc != QW_STATUS_OK) return rc;
    if (seek_table_length > size - seek_table_offset)
        return QW_STATUS_ERR_FORMAT;
    seek_table_end = seek_table_offset + seek_table_length;
    source->bit_table_offset = seek_table_end;
    if (source->version < 3810u) {
        if (frame_count > size - seek_table_end) return QW_STATUS_ERR_FORMAT;
        if (frame_count > SIZE_MAX - seek_table_end) return QW_STATUS_ERR_FORMAT;
        seek_table_end += frame_count;
    } else {
        source->bit_table_offset = 0u;
    }
    first_frame_offset = seek_table_end;

    rc = qw_audio_trailing_tags_audio_end(bytes, size, &trailing_end);
    if (rc != QW_STATUS_OK || wav_tail_length > trailing_end)
        return rc == QW_STATUS_OK ? QW_STATUS_ERR_FORMAT : rc;
    trailing_end -= wav_tail_length;
    if (first_frame_offset > trailing_end) return QW_STATUS_ERR_FORMAT;

    if (source->format_flags & APE_FLAG_8_BIT)
        source->bits_per_sample = 8u;
    else if (source->format_flags & APE_FLAG_24_BIT)
        source->bits_per_sample = 24u;
    else
        source->bits_per_sample = 16u;
    if (source->version >= 3950u) {
        blocks_per_frame = UINT64_C(73728) * 4u;
    } else if (source->version >= 3900u ||
               (source->version >= 3800u &&
                source->compression_type >= 4000u)) {
        blocks_per_frame = 73728u;
    } else {
        blocks_per_frame = 9216u;
    }
    if (blocks_per_frame > UINT32_MAX) return QW_STATUS_ERR_FORMAT;

    source->blocks_per_frame = (uint32_t)blocks_per_frame;
    source->final_frame_blocks = final_frame_blocks;
    source->frame_count = frame_count;
    source->seek_table_offset = seek_table_offset;
    source->first_frame_offset = first_frame_offset;
    source->frame_data_end = trailing_end;
    return QW_STATUS_OK;
}

int qw_audio_ape_header_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioApeSource *source,
                              QwConvertReport *report)
{
    size_t stream_offset = 0u;
    uint16_t version;
    uint64_t sample_count;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (!qw_audio_id3_skip_leading_v2(bytes, size, &stream_offset))
        return QW_STATUS_ERR_FORMAT;
    if (size - stream_offset < 6u ||
        memcmp(bytes + stream_offset, "MAC ", 4u) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    version = qw_read_u16_le(bytes + stream_offset + 4u);
    if (version < APE_MIN_VERSION || version > APE_MAX_VERSION)
        return QW_STATUS_ERR_UNSUPPORTED;

    source->data = bytes;
    source->size = size;
    source->stream_offset = stream_offset;
    rc = version >= APE_MODERN_VERSION
        ? ape_parse_modern_header(bytes, size, stream_offset, source)
        : ape_parse_legacy_header(bytes, size, stream_offset, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!ape_shape_supported(source)) return QW_STATUS_ERR_UNSUPPORTED;
    rc = ape_sample_count(source, &sample_count);
    if (rc != QW_STATUS_OK) return rc;
    source->sample_count = sample_count;
    source->has_metadata = stream_offset != 0u ||
                           source->frame_data_end < size;
    return QW_STATUS_OK;
}
