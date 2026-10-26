#include "audio_pcm_parser.h"
#include "audio_aiff_chunks.h"
#include "audio_pcm_shape.h"
#include "audio_wave_chunks.h"
#include "audio_wave_format.h"

#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

static int aiff_pcm_tag_equal(const uint8_t tag[4], const char value[4])
{
    size_t i;
    for (i = 0u; i < 4u; ++i) {
        unsigned char left = tag[i];
        unsigned char right = (unsigned char)value[i];
        if (left >= (unsigned char)'A' && left <= (unsigned char)'Z')
            left = (unsigned char)(left + ((unsigned char)'a' - 'A'));
        if (right >= (unsigned char)'A' && right <= (unsigned char)'Z')
            right = (unsigned char)(right + ((unsigned char)'a' - 'A'));
        if (left != right) return 0;
    }
    return 1;
}

static int audio_aiff_pcm_parse_compression(
    const QwAudioAiffChunks *chunks,
    int *floating_point,
    int *little_endian,
    int *unsigned_8bit,
    QwConvertReport *report)
{
    const uint8_t *tag;
    if (!chunks || !floating_point || !little_endian || !unsigned_8bit)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    *floating_point = 0;
    *little_endian = 0;
    *unsigned_8bit = 0;
    if (!chunks->aifc) return QW_STATUS_OK;
    tag = chunks->compression_type;

    if (aiff_pcm_tag_equal(tag, "NONE")) return QW_STATUS_OK;
    if (aiff_pcm_tag_equal(tag, "twos")) {
        return chunks->sample_bits == 16u ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    if (aiff_pcm_tag_equal(tag, "sowt")) {
        if (chunks->sample_bits != 16u) return QW_STATUS_ERR_FORMAT;
        *little_endian = 1;
        return QW_STATUS_OK;
    }
    if (aiff_pcm_tag_equal(tag, "raw ")) {
        if (chunks->sample_bits != 8u) return QW_STATUS_ERR_FORMAT;
        *unsigned_8bit = 1;
        return QW_STATUS_OK;
    }
    if (aiff_pcm_tag_equal(tag, "in24")) {
        return chunks->sample_bits == 24u ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    if (aiff_pcm_tag_equal(tag, "in32")) {
        return chunks->sample_bits == 32u ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    if (aiff_pcm_tag_equal(tag, "fl32")) {
        if (chunks->sample_bits != 32u) return QW_STATUS_ERR_FORMAT;
        *floating_point = 1;
        return QW_STATUS_OK;
    }
    if (aiff_pcm_tag_equal(tag, "fl64")) {
        if (chunks->sample_bits != 64u) return QW_STATUS_ERR_FORMAT;
        *floating_point = 1;
        return QW_STATUS_OK;
    }
    if (report) {
        (void)snprintf(report->note, sizeof(report->note),
                       "native AIFF parser accepts uncompressed integer or IEEE float PCM only");
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

int qw_audio_pcm_parse_wave(const uint8_t *bytes,
                            size_t size,
                            QwAudioPcmSource *source,
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
    return qw_audio_pcm_parse_wave_chunks(&chunks, source, report);
}

int qw_audio_pcm_parse_wave_chunks(const QwAudioWaveChunks *chunk_view,
                                   QwAudioPcmSource *source,
                                   QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWavePcmFormat wave_format = {0};
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    rc = qw_audio_wave_parse_pcm_format(chunks.format, chunks.format_size,
                                        chunks.little_endian,
                                        &wave_format, report);
    if (rc != QW_STATUS_OK) return rc;
    if (chunks.data_size % wave_format.block_align != 0u)
        return QW_STATUS_ERR_FORMAT;
    source->has_metadata = chunks.has_metadata;
    source->samples = chunks.data;
    source->sample_bytes = chunks.data_size;
    source->sample_rate = wave_format.sample_rate;
    source->frames = chunks.data_size / wave_format.block_align;
    source->channel_mask = wave_format.channel_mask;
    source->channel_layout_unspecified =
        wave_format.channel_layout_unspecified;
    if (!source->frames) return QW_STATUS_ERR_FORMAT;
    source->channels = wave_format.channels;
    source->sample_bits = wave_format.sample_bits;
    source->little_endian = chunks.little_endian;
    source->floating_point = wave_format.floating_point;
    source->unsigned_8bit = wave_format.unsigned_8bit;
    return QW_STATUS_OK;
}

int qw_audio_pcm_parse_aiff(const uint8_t *bytes,
                            size_t size,
                            QwAudioPcmSource *source,
                            QwConvertReport *report)
{
    QwAudioAiffChunks chunks = {0};
    int floating_point = 0;
    int little_endian = 0;
    int unsigned_8bit = 0;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_aiff_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) return rc;
    if (chunks.has_apcm) return QW_STATUS_ERR_UNSUPPORTED;
    rc = audio_aiff_pcm_parse_compression(
        &chunks, &floating_point, &little_endian, &unsigned_8bit, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!qw_audio_pcm_shape_supported(chunks.channels, chunks.sample_rate,
                                      chunks.sample_bits, floating_point)) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native PCM parser supports 1-64 channels, positive int-range sample rates, integer 8/16/24/32-bit or float 32/64-bit samples");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    {
        uint64_t frame_bytes = (uint64_t)chunks.channels *
                               (chunks.sample_bits / 8u);
        uint64_t expected_bytes = frame_bytes * chunks.frame_count;
        if (!chunks.frame_count || expected_bytes > SIZE_MAX ||
            expected_bytes != chunks.sample_bytes) return QW_STATUS_ERR_FORMAT;
    }
    source->has_metadata = chunks.has_metadata;
    source->samples = chunks.sample_data;
    source->sample_bytes = chunks.sample_bytes;
    source->sample_rate = chunks.sample_rate;
    source->frames = chunks.frame_count;
    source->channels = chunks.channels;
    source->sample_bits = chunks.sample_bits;
    source->little_endian = little_endian;
    source->floating_point = floating_point;
    source->unsigned_8bit = unsigned_8bit;
    return QW_STATUS_OK;
}
