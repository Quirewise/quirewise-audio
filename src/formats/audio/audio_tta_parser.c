#include "audio_tta_parser.h"

#include "audio_id3_tag.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

#define AUDIO_TTA_HEADER_SIZE 22u
#define AUDIO_TTA_HEADER_FIELDS_SIZE 18u

static uint32_t audio_tta_crc32(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t i;
    for (i = 0u; i < size; ++i) {
        unsigned bit;
        crc ^= bytes[i];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

int qw_audio_tta_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioTtaSource *source,
                       QwConvertReport *report)
{
    uint16_t format;
    uint32_t declared_sample_count;
    uint64_t frame_count_64;
    size_t frame_count;
    size_t table_bytes;
    size_t frame_data_offset;
    size_t frame_data_end;
    size_t id3_size;
    size_t frame_index;
    uint64_t frame_length;
    int has_leading_metadata;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (!qw_audio_id3_skip_leading_v2(bytes, size, &id3_size))
        return QW_STATUS_ERR_FORMAT;
    has_leading_metadata = id3_size != 0u;
    bytes += id3_size;
    size -= id3_size;
    if (size < AUDIO_TTA_HEADER_SIZE || memcmp(bytes, "TTA1", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;

    format = qw_read_u16_le(bytes + 4u);
    source->channels = qw_read_u16_le(bytes + 6u);
    source->bits_per_sample = qw_read_u16_le(bytes + 8u);
    source->sample_rate = qw_read_u32_le(bytes + 10u);
    declared_sample_count = qw_read_u32_le(bytes + 14u);
    if (format != 1u) return QW_STATUS_ERR_UNSUPPORTED;
    if (source->channels == 0u || source->channels > 16u ||
        source->bits_per_sample == 0u ||
        source->sample_rate == 0u || source->sample_rate > 1000000u ||
        declared_sample_count == 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (source->bits_per_sample > 24u) return QW_STATUS_ERR_UNSUPPORTED;
    if (audio_tta_crc32(bytes, AUDIO_TTA_HEADER_FIELDS_SIZE) !=
        qw_read_u32_le(bytes + AUDIO_TTA_HEADER_FIELDS_SIZE)) {
        return QW_STATUS_ERR_FORMAT;
    }

    frame_length = (uint64_t)source->sample_rate * 256u / 245u;
    if (!frame_length || frame_length > UINT32_MAX) return QW_STATUS_ERR_FORMAT;
    frame_count_64 = declared_sample_count / frame_length +
                     (declared_sample_count % frame_length != 0u);
    if (!frame_count_64 || frame_count_64 > SIZE_MAX / sizeof(uint32_t))
        return QW_STATUS_ERR_FORMAT;
    frame_count = (size_t)frame_count_64;
    table_bytes = frame_count * sizeof(uint32_t);
    if (table_bytes > size - AUDIO_TTA_HEADER_SIZE ||
        size - AUDIO_TTA_HEADER_SIZE - table_bytes < sizeof(uint32_t)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (audio_tta_crc32(bytes + AUDIO_TTA_HEADER_SIZE, table_bytes) !=
        qw_read_u32_le(bytes + AUDIO_TTA_HEADER_SIZE + table_bytes)) {
        return QW_STATUS_ERR_FORMAT;
    }

    frame_data_offset = AUDIO_TTA_HEADER_SIZE + table_bytes + sizeof(uint32_t);
    frame_data_end = frame_data_offset;
    for (frame_index = 0u; frame_index < frame_count; ++frame_index) {
        size_t entry_offset = AUDIO_TTA_HEADER_SIZE +
                              frame_index * sizeof(uint32_t);
        uint32_t frame_size = qw_read_u32_le(bytes + entry_offset);
        if (frame_size < sizeof(uint32_t) ||
            (size_t)frame_size > size - frame_data_end) {
            return QW_STATUS_ERR_FORMAT;
        }
        frame_data_end += frame_size;
    }

    source->data = bytes;
    source->size = size;
    source->frame_table_offset = AUDIO_TTA_HEADER_SIZE;
    source->frame_data_offset = frame_data_offset;
    source->frame_data_end = frame_data_end;
    source->frame_count = frame_count;
    source->sample_count = declared_sample_count;
    source->frame_length = (uint32_t)frame_length;
    source->has_metadata = has_leading_metadata || frame_data_end < size;
    memcpy(source->header, bytes, sizeof(source->header));
    return QW_STATUS_OK;
}

int qw_audio_tta_next_frame(const QwAudioTtaSource *source,
                            size_t frame_index,
                            size_t position,
                            QwAudioTtaFrame *frame)
{
    size_t entry_offset;
    size_t frame_size;
    uint64_t prior_samples;
    uint64_t remaining_samples;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (frame_index == source->frame_count) {
        return position == source->frame_data_end ? 0 : QW_STATUS_ERR_FORMAT;
    }
    if (frame_index > source->frame_count ||
        source->frame_table_offset > source->size ||
        frame_index > (source->size - source->frame_table_offset) /
                          sizeof(uint32_t)) {
        return QW_STATUS_ERR_FORMAT;
    }
    entry_offset = source->frame_table_offset +
                   frame_index * sizeof(uint32_t);
    if (sizeof(uint32_t) > source->size - entry_offset)
        return QW_STATUS_ERR_FORMAT;
    frame_size = qw_read_u32_le(source->data + entry_offset);
    if (position < source->frame_data_offset ||
        position > source->frame_data_end ||
        frame_size < sizeof(uint32_t) ||
        frame_size > source->frame_data_end - position) {
        return QW_STATUS_ERR_FORMAT;
    }
    prior_samples = (uint64_t)frame_index * source->frame_length;
    if (prior_samples >= source->sample_count) return QW_STATUS_ERR_FORMAT;
    remaining_samples = source->sample_count - prior_samples;
    frame->data = source->data + position;
    frame->size = frame_size;
    frame->next_position = position + frame_size;
    frame->sample_count = (uint32_t)(remaining_samples < source->frame_length
        ? remaining_samples : source->frame_length);
    return 1;
}
