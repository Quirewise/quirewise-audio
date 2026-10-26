#include "audio_wave_ima_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_chunks.h"
#include "audio_wave_ima_format.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave_ima_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioWaveImaSource *source,
                            QwConvertReport *report)
{
    QwAudioWaveChunks chunks = {0};
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_wave_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) {
        memset(source, 0, sizeof(*source));
        return rc;
    }
    return qw_audio_wave_ima_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_ima_parse_chunks(const QwAudioWaveChunks *chunk_view,
                                  QwAudioWaveImaSource *source,
                                  QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWaveImaFormat format;
    size_t frame_count;
    uint64_t capacity_samples;
    uint64_t sample_count;
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    if (chunks.has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    if (!chunks.little_endian) return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_wave_ima_format_parse(chunks.format, chunks.format_size,
                                        &format, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.data_size || chunks.data_size % format.block_align != 0u)
        return QW_STATUS_ERR_FORMAT;
    frame_count = chunks.data_size / format.block_align;
    if ((uint64_t)frame_count > UINT64_MAX / format.samples_per_block)
        return QW_STATUS_ERR_FORMAT;
    capacity_samples = (uint64_t)frame_count * format.samples_per_block;
    sample_count = capacity_samples;
    if (chunks.fact) {
        if (chunks.fact_size < 4u) return QW_STATUS_ERR_FORMAT;
        if (!chunks.fact_sample_count_unknown) {
            sample_count = qw_audio_wave_fact_sample_count(&chunks);
            if (!sample_count || sample_count > capacity_samples ||
                sample_count <= capacity_samples - format.samples_per_block) {
                return QW_STATUS_ERR_FORMAT;
            }
        }
    }
    rc = qw_audio_wave_ima_validate_blocks(chunks.data, chunks.data_size,
                                           format.block_align,
                                           format.channels);
    if (rc != QW_STATUS_OK) return rc;

    source->data = chunks.data;
    source->size = chunks.data_size;
    source->frame_count = frame_count;
    source->sample_count = sample_count;
    source->sample_rate = format.sample_rate;
    source->block_align = format.block_align;
    source->samples_per_block = format.samples_per_block;
    source->channels = format.channels;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_wave_ima_next_frame(const QwAudioWaveImaSource *source,
                                 size_t frame_index,
                                 QwAudioWaveImaFrame *frame)
{
    size_t block_offset;
    uint64_t first_sample;
    uint64_t remaining_samples;
    int rc;
    if (!source || !frame || !source->data || !source->block_align ||
        !source->samples_per_block) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    memset(frame, 0, sizeof(*frame));
    if (frame_index == source->frame_count) return 0;
    if (frame_index > source->frame_count ||
        frame_index > SIZE_MAX / source->block_align ||
        (uint64_t)frame_index > UINT64_MAX / source->samples_per_block) {
        return QW_STATUS_ERR_FORMAT;
    }
    block_offset = frame_index * (size_t)source->block_align;
    if (block_offset > source->size ||
        source->block_align > source->size - block_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_wave_ima_validate_blocks(source->data + block_offset,
                                           source->block_align,
                                           source->block_align,
                                           source->channels);
    if (rc != QW_STATUS_OK) return rc;
    first_sample = (uint64_t)frame_index * source->samples_per_block;
    if (first_sample >= source->sample_count) return QW_STATUS_ERR_FORMAT;
    remaining_samples = source->sample_count - first_sample;
    frame->data = source->data + block_offset;
    frame->size = source->block_align;
    frame->sample_count = remaining_samples < source->samples_per_block
        ? (uint32_t)remaining_samples : source->samples_per_block;
    frame->next_frame_index = frame_index + 1u;
    return 1;
}
