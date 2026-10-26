#include "audio_wave_ms_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_chunks.h"
#include "audio_wave_ms_block_validator.h"
#include "audio_wave_ms_format.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave_ms_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioWaveMsSource *source,
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
    return qw_audio_wave_ms_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_ms_parse_chunks(const QwAudioWaveChunks *chunk_view,
                                 QwAudioWaveMsSource *source,
                                 QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWaveMsFormat format;
    uint64_t capacity_samples;
    uint64_t sample_count;
    size_t frame_count;
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    if (chunks.has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    if (!chunks.little_endian) return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_wave_ms_format_parse(chunks.format, chunks.format_size,
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

    source->data = chunks.data;
    source->size = chunks.data_size;
    source->frame_count = frame_count;
    source->sample_count = sample_count;
    source->sample_rate = format.sample_rate;
    source->block_align = format.block_align;
    source->samples_per_block = format.samples_per_block;
    source->coefficient_count = format.coefficient_count;
    source->coefficients = format.coefficients;
    source->channels = format.channels;
    source->has_standard_coefficients = format.has_standard_coefficients;
    source->has_metadata = chunks.has_metadata;
    rc = qw_audio_wave_ms_validate_blocks(source->data, source->size,
                                          format.block_align, format.channels,
                                          format.coefficient_count);
    if (rc != QW_STATUS_OK) {
        memset(source, 0, sizeof(*source));
        return rc;
    }
    return QW_STATUS_OK;
}

int qw_audio_wave_ms_next_frame(const QwAudioWaveMsSource *source,
                                size_t frame_index,
                                QwAudioWaveMsFrame *frame)
{
    uint64_t sample_offset;
    uint64_t remaining_samples;
    uint32_t frame_samples;

    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (!source->data || frame_index >= source->frame_count ||
        !source->block_align || !source->samples_per_block) {
        return 0;
    }
    if ((uint64_t)frame_index > UINT64_MAX / source->samples_per_block)
        return QW_STATUS_ERR_FORMAT;
    sample_offset = (uint64_t)frame_index * source->samples_per_block;
    if (sample_offset >= source->sample_count) return 0;
    remaining_samples = source->sample_count - sample_offset;
    frame_samples = remaining_samples < source->samples_per_block
        ? (uint32_t)remaining_samples : source->samples_per_block;
    frame->data = source->data + frame_index * (size_t)source->block_align;
    frame->size = source->block_align;
    frame->next_frame_index = frame_index + 1u;
    frame->sample_count = frame_samples;
    return 1;
}
