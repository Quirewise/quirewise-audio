#include "audio_wavpack_parser.h"

#include "audio_wavpack_internal.h"
#include "audio_trailing_tags.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

#define AUDIO_WAVPACK_HEADER_SIZE 32u

typedef struct QwAudioWavpackBlockHeader {
    size_t frame_size;
    uint32_t total_samples;
    uint32_t block_index;
    uint32_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t version;
    uint32_t flags;
} QwAudioWavpackBlockHeader;

typedef struct QwAudioWavpackBlockGroup {
    size_t size;
    uint32_t total_samples;
    uint32_t block_index;
    uint32_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t version;
    int is_dsd;
} QwAudioWavpackBlockGroup;

static int audio_wavpack_parse_block(const uint8_t *bytes,
                                     size_t size,
                                     QwAudioWavpackBlockHeader *header)
{
    uint32_t declared_block_size;
    uint32_t sample_rate_index;
    uint32_t channels;
    int rc;
    if (!bytes || !header || size < AUDIO_WAVPACK_HEADER_SIZE)
        return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes, "wvpk", 4u) != 0) return QW_STATUS_ERR_UNSUPPORTED;
    declared_block_size = qw_read_u32_le(bytes + 4u);
    if (declared_block_size < 24u ||
        (uint64_t)declared_block_size + 8u > size) {
        return QW_STATUS_ERR_FORMAT;
    }
    header->version = qw_read_u16_le(bytes + 8u);
    if (header->version < 0x0402u || header->version > 0x0410u)
        return QW_STATUS_ERR_UNSUPPORTED;
    header->total_samples = qw_read_u32_le(bytes + 12u);
    header->block_index = qw_read_u32_le(bytes + 16u);
    header->sample_count = qw_read_u32_le(bytes + 20u);
    header->flags = qw_read_u32_le(bytes + 24u);
    header->frame_size = (size_t)declared_block_size + 8u;
    if (!header->sample_count) {
        header->sample_rate = 0u;
        header->channels = 0u;
        header->bits_per_sample = 0u;
        return QW_STATUS_OK;
    }
    if ((header->flags & AUDIO_WAVPACK_DSD_FLAG) != 0u &&
        (header->flags & AUDIO_WAVPACK_HYBRID_FLAG) != 0u)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_index = (header->flags >> 23u) & 0x0fu;
    if (sample_rate_index == 15u) {
        rc = audio_wavpack_parse_custom_sample_rate(
            bytes + AUDIO_WAVPACK_HEADER_SIZE,
            (size_t)declared_block_size - 24u, &header->sample_rate);
        if (rc != QW_STATUS_OK) return rc;
    } else {
        header->sample_rate = audio_wavpack_sample_rates[sample_rate_index];
        if (!header->sample_rate) return QW_STATUS_ERR_UNSUPPORTED;
    }
    if ((header->flags & AUDIO_WAVPACK_DSD_FLAG) != 0u) {
        uint32_t rate_factor;
        rc = audio_wavpack_parse_dsd_rate_factor(
            bytes + AUDIO_WAVPACK_HEADER_SIZE,
            (size_t)declared_block_size - 24u, &rate_factor);
        if (rc != QW_STATUS_OK) return rc;
        if (rate_factor > (uint32_t)INT_MAX / header->sample_rate)
            return QW_STATUS_ERR_UNSUPPORTED;
        header->sample_rate *= rate_factor;
    }
    channels = (header->flags & AUDIO_WAVPACK_MONO_FLAG) ? 1u : 2u;
    header->channels = (uint16_t)channels;
    header->bits_per_sample =
        (uint16_t)(((header->flags & 3u) + 1u) * 8u);
    return QW_STATUS_OK;
}

static int audio_wavpack_parse_block_group(const uint8_t *bytes,
                                           size_t size,
                                           QwAudioWavpackBlockGroup *group)
{
    QwAudioWavpackBlockHeader header;
    size_t position = 0u;
    uint32_t channels = 0u;
    int first_block = 1;
    int rc;
    if (!bytes || !group) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(group, 0, sizeof(*group));
    for (;;) {
        size_t remaining;
        if (position > size) return QW_STATUS_ERR_FORMAT;
        remaining = size - position;
        rc = audio_wavpack_parse_block(bytes + position, remaining, &header);
        if (rc != QW_STATUS_OK) return rc;
        if (!header.sample_count) return QW_STATUS_ERR_UNSUPPORTED;
        if ((first_block &&
             (header.flags & AUDIO_WAVPACK_INITIAL_FLAG) == 0u) ||
            (!first_block &&
             (header.flags & AUDIO_WAVPACK_INITIAL_FLAG) != 0u)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (header.total_samples == 0u ||
            (header.total_samples != UINT32_MAX &&
             (header.block_index >= header.total_samples ||
              header.sample_count >
                  header.total_samples - header.block_index))) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (first_block) {
            group->total_samples = header.total_samples;
            group->block_index = header.block_index;
            group->sample_count = header.sample_count;
            group->sample_rate = header.sample_rate;
            group->bits_per_sample = header.bits_per_sample;
            group->version = header.version;
            group->is_dsd =
                (header.flags & AUDIO_WAVPACK_DSD_FLAG) != 0u;
        } else if (header.total_samples != group->total_samples ||
                   header.block_index != group->block_index ||
                   header.sample_count != group->sample_count ||
                   header.sample_rate != group->sample_rate ||
                   header.bits_per_sample != group->bits_per_sample ||
                   header.version != group->version ||
                   (((header.flags & AUDIO_WAVPACK_DSD_FLAG) != 0u) !=
                    group->is_dsd)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (channels > (uint32_t)QW_AUDIO_WAVPACK_MAX_CHANNELS -
                           (uint32_t)header.channels ||
            header.frame_size > remaining ||
            header.frame_size > SIZE_MAX - position) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        channels += header.channels;
        position += header.frame_size;
        if ((header.flags & AUDIO_WAVPACK_FINAL_FLAG) != 0u) break;
        if (position >= size) return QW_STATUS_ERR_FORMAT;
        first_block = 0;
    }
    if (!channels || position == 0u) return QW_STATUS_ERR_FORMAT;
    group->channels = (uint16_t)channels;
    group->size = position;
    return QW_STATUS_OK;
}

int qw_audio_wavpack_next_frame(const QwAudioWavpackSource *source,
                                size_t position,
                                QwAudioWavpackFrame *frame)
{
    QwAudioWavpackBlockGroup group;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;
    rc = audio_wavpack_parse_block_group(source->data + position,
                                         source->frame_data_end - position,
                                         &group);
    if (rc != QW_STATUS_OK) return rc;
    if (group.size > source->frame_data_end - position ||
        group.version != qw_read_u16_le(source->version) ||
        group.sample_rate != source->sample_rate ||
        group.channels != source->channels ||
        group.bits_per_sample != source->bits_per_sample ||
        group.is_dsd != source->is_dsd ||
        group.total_samples != source->declared_total_samples) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->data = source->data + position;
    frame->size = group.size;
    frame->next_position = position + group.size;
    frame->sample_count = group.sample_count;
    frame->block_index = group.block_index;
    return 1;
}

int qw_audio_wavpack_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioWavpackSource *source,
                           QwConvertReport *report)
{
    QwAudioWavpackBlockGroup first_group;
    QwAudioWavpackBlockHeader preamble_block;
    size_t position = 0u;
    size_t audio_end;
    uint64_t parsed_samples = 0u;
    uint64_t expected_block_index;
    uint64_t expected_samples = 0u;
    int expected_samples_known;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_trailing_tags_audio_end(bytes, size, &audio_end);
    if (rc != QW_STATUS_OK) return rc;
    while (position < audio_end) {
        rc = audio_wavpack_parse_block(bytes + position,
                                       audio_end - position,
                                       &preamble_block);
        if (rc != QW_STATUS_OK) return rc;
        if (preamble_block.sample_count) break;
        position += preamble_block.frame_size;
    }
    if (position >= audio_end) return QW_STATUS_ERR_FORMAT;
    rc = audio_wavpack_parse_block_group(bytes + position,
                                         audio_end - position, &first_group);
    if (rc != QW_STATUS_OK) return rc;

    source->data = bytes;
    source->size = size;
    source->first_frame_offset = position;
    source->frame_data_end = audio_end;
    source->sample_count = 0u;
    source->declared_total_samples = first_group.total_samples;
    source->sample_rate = first_group.sample_rate;
    source->channels = first_group.channels;
    source->bits_per_sample = first_group.bits_per_sample;
    source->is_dsd = (uint8_t)first_group.is_dsd;
    source->version[0] = bytes[position + 8u];
    source->version[1] = bytes[position + 9u];
    expected_block_index = first_group.block_index;
    expected_samples_known = first_group.total_samples != UINT32_MAX;
    if (expected_samples_known) {
        expected_samples =
            (uint64_t)first_group.total_samples - first_group.block_index;
    }

    position = source->first_frame_offset;
    while (position < audio_end) {
        QwAudioWavpackBlockHeader next_block;
        QwAudioWavpackFrame frame;
        rc = audio_wavpack_parse_block(bytes + position,
                                       audio_end - position, &next_block);
        if (rc != QW_STATUS_OK) return rc;
        if (!next_block.sample_count) {
            if (!parsed_samples) return QW_STATUS_ERR_FORMAT;
            source->frame_data_end = position;
            break;
        }
        rc = qw_audio_wavpack_next_frame(source, position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (expected_samples_known &&
            (frame.block_index != expected_block_index ||
             frame.sample_count > UINT32_MAX - expected_block_index ||
             frame.sample_count > expected_samples - parsed_samples)) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (expected_samples_known)
            expected_block_index += frame.sample_count;
        parsed_samples += frame.sample_count;
        position = frame.next_position;
        if (expected_samples_known && parsed_samples == expected_samples) {
            source->frame_data_end = position;
            break;
        }
    }
    if (!parsed_samples ||
        (expected_samples_known && parsed_samples != expected_samples)) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->sample_count = parsed_samples;
    return QW_STATUS_OK;
}
