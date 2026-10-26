#include "audio_ape_parser.h"

#include "audio_ape_header_parser.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

static uint64_t audio_ape_frame_position(const QwAudioApeSource *source,
                                         size_t frame_index)
{
    uint32_t relative_offset;
    size_t entry_offset;
    if (frame_index == 0u) return source->first_frame_offset;
    entry_offset = source->seek_table_offset + frame_index * 4u;
    relative_offset = qw_read_u32_le(source->data + entry_offset);
    return (uint64_t)source->stream_offset + relative_offset;
}

static int audio_ape_frame_layout(const QwAudioApeSource *source,
                                  size_t frame_index,
                                  size_t *packet_offset,
                                  size_t *packet_size,
                                  uint32_t *sample_count,
                                  uint32_t *skip)
{
    uint64_t frame_start;
    uint64_t frame_end;
    uint64_t packet_start;
    uint64_t raw_size;
    uint64_t aligned_size;
    uint32_t skip_bytes;
    uint32_t skip_value;
    if (!source || frame_index >= source->frame_count || !packet_offset ||
        !packet_size || !sample_count || !skip) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    frame_start = audio_ape_frame_position(source, frame_index);
    frame_end = frame_index + 1u < source->frame_count
        ? audio_ape_frame_position(source, frame_index + 1u)
        : source->frame_data_end;
    if (frame_start < source->first_frame_offset ||
        frame_start >= source->frame_data_end || frame_end <= frame_start ||
        frame_end > source->frame_data_end) {
        return QW_STATUS_ERR_FORMAT;
    }
    skip_bytes = frame_index == 0u
        ? 0u : (uint32_t)((frame_start - source->first_frame_offset) & 3u);
    skip_value = skip_bytes;
    if (source->version < 3810u) {
        uint8_t bit_skip;
        if (!source->bit_table_offset ||
            source->bit_table_offset > source->size ||
            frame_index >= source->size - source->bit_table_offset) {
            return QW_STATUS_ERR_FORMAT;
        }
        bit_skip = source->data[source->bit_table_offset + frame_index];
        if (bit_skip > 7u) return QW_STATUS_ERR_FORMAT;
        skip_value = skip_bytes * 8u + bit_skip;
    }
    if (frame_start < skip_bytes) return QW_STATUS_ERR_FORMAT;
    packet_start = frame_start - skip_bytes;
    raw_size = frame_end - frame_start;
    if (source->version < 3810u &&
        frame_index + 1u < source->frame_count &&
        source->data[source->bit_table_offset + frame_index + 1u] != 0u) {
        if (raw_size > UINT64_MAX - 4u) return QW_STATUS_ERR_FORMAT;
        raw_size += 4u;
    }
    if (raw_size > UINT64_MAX - skip_bytes) return QW_STATUS_ERR_FORMAT;
    raw_size += skip_bytes;
    if (raw_size > UINT64_MAX - 3u) return QW_STATUS_ERR_FORMAT;
    aligned_size = (raw_size + 3u) & ~UINT64_C(3);
    if (packet_start > source->frame_data_end ||
        aligned_size > (uint64_t)source->frame_data_end - packet_start) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (aligned_size == 0u || aligned_size > (uint64_t)INT_MAX - 8u ||
        packet_start > SIZE_MAX || aligned_size > SIZE_MAX) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *packet_offset = (size_t)packet_start;
    *packet_size = (size_t)aligned_size;
    *sample_count = frame_index + 1u == source->frame_count
        ? source->final_frame_blocks : source->blocks_per_frame;
    *skip = skip_value;
    return *sample_count ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_ape_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioApeSource *source,
                       QwConvertReport *report)
{
    size_t i;
    int rc;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_ape_header_parse(bytes, size, source, report);
    if (rc != QW_STATUS_OK) return rc;
    source->decoder_extradata[0] = (uint8_t)(source->version & 0xffu);
    source->decoder_extradata[1] = (uint8_t)(source->version >> 8u);
    source->decoder_extradata[2] =
        (uint8_t)(source->compression_type & 0xffu);
    source->decoder_extradata[3] =
        (uint8_t)(source->compression_type >> 8u);
    source->decoder_extradata[4] = (uint8_t)(source->format_flags & 0xffu);
    source->decoder_extradata[5] = (uint8_t)(source->format_flags >> 8u);
    for (i = 0u; i < source->frame_count; ++i) {
        size_t packet_offset;
        size_t packet_size;
        uint32_t frame_samples;
        uint32_t skip;
        rc = audio_ape_frame_layout(source, i, &packet_offset, &packet_size,
                                    &frame_samples, &skip);
        if (rc != QW_STATUS_OK) return rc;
        (void)packet_offset;
        (void)packet_size;
        (void)frame_samples;
        (void)skip;
    }
    return QW_STATUS_OK;
}

int qw_audio_ape_next_frame(const QwAudioApeSource *source,
                            size_t frame_index,
                            QwAudioApeFrame *frame)
{
    size_t packet_offset;
    size_t packet_size;
    uint32_t sample_count;
    uint32_t skip;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (frame_index >= source->frame_count) return 0;
    rc = audio_ape_frame_layout(source, frame_index, &packet_offset,
                                &packet_size, &sample_count, &skip);
    if (rc != QW_STATUS_OK) return rc;
    frame->data = source->data + packet_offset;
    frame->size = packet_size;
    frame->sample_count = sample_count;
    frame->skip = skip;
    return 1;
}
