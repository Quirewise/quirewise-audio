#include "audio_wave_g711_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_chunks.h"
#include "audio_wave_subformat.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    AUDIO_WAVE_G711_ALAW_TAG = 0x0006u,
    AUDIO_WAVE_G711_MULAW_TAG = 0x0007u,
    AUDIO_WAVE_G711_BITS_PER_SAMPLE = 8u
};

int qw_audio_wave_g711_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioWaveG711Source *source,
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
    return qw_audio_wave_g711_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_g711_parse_chunks(const QwAudioWaveChunks *chunk_view,
                                    QwAudioWaveG711Source *source,
                                    QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWaveSubformat subformat;
    const uint8_t *format;
    uint16_t format_tag;
    uint16_t channels;
    uint16_t block_align;
    uint16_t sample_bits;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint64_t sample_capacity;
    uint64_t sample_count;
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    if (chunks.has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    if (chunks.format_size < 2u) return QW_STATUS_ERR_FORMAT;

    format = chunks.format;
    rc = qw_audio_wave_parse_subformat(format, chunks.format_size,
                                       chunks.little_endian,
                                       &subformat, report);
    if (rc != QW_STATUS_OK) return rc;
    format_tag = subformat.format_tag;
    if (format_tag != AUDIO_WAVE_G711_ALAW_TAG &&
        format_tag != AUDIO_WAVE_G711_MULAW_TAG) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (chunks.format_size < 16u) return QW_STATUS_ERR_FORMAT;
    if (!subformat.extensible && chunks.format_size >= 18u) {
        uint16_t extension_size = chunks.little_endian
            ? qw_read_u16_le(format + 16u)
            : qw_read_u16_be(format + 16u);
        if ((size_t)extension_size > chunks.format_size - 18u)
            return QW_STATUS_ERR_FORMAT;
    }
    channels = chunks.little_endian
        ? qw_read_u16_le(format + 2u) : qw_read_u16_be(format + 2u);
    sample_rate = chunks.little_endian
        ? qw_read_u32_le(format + 4u) : qw_read_u32_be(format + 4u);
    byte_rate = chunks.little_endian
        ? qw_read_u32_le(format + 8u) : qw_read_u32_be(format + 8u);
    block_align = chunks.little_endian
        ? qw_read_u16_le(format + 12u) : qw_read_u16_be(format + 12u);
    sample_bits = subformat.sample_bits;
    if (!channels || channels > 8u || sample_rate < 4000u ||
        sample_rate > 384000u || sample_bits != AUDIO_WAVE_G711_BITS_PER_SAMPLE ||
        block_align != channels ||
        (uint64_t)sample_rate * channels > UINT32_MAX ||
        byte_rate != sample_rate * (uint32_t)channels) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native WAVE G.711 parser supports 8-bit A-law or mu-law with 1-8 channels");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!chunks.data_size || chunks.data_size % channels != 0u)
        return QW_STATUS_ERR_FORMAT;

    sample_capacity = chunks.data_size / channels;
    sample_count = sample_capacity;
    if (chunks.fact) {
        if (chunks.fact_size < 4u) return QW_STATUS_ERR_FORMAT;
        if (!chunks.fact_sample_count_unknown) {
            sample_count = qw_audio_wave_fact_sample_count(&chunks);
            if (!sample_count || sample_count > sample_capacity)
                return QW_STATUS_ERR_FORMAT;
        }
    }
    source->data = chunks.data;
    source->size = chunks.data_size;
    source->sample_count = sample_count;
    source->sample_rate = sample_rate;
    source->channel_mask = subformat.channel_mask;
    source->channels = channels;
    source->codec = format_tag == AUDIO_WAVE_G711_ALAW_TAG
        ? QW_AUDIO_WAVE_G711_ALAW : QW_AUDIO_WAVE_G711_MULAW;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}
