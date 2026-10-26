#include "audio_wave_mpeg_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_chunks.h"
#include "audio_wave_mpeg_format.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave_mpeg_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioWaveMpegSource *source,
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
    return qw_audio_wave_mpeg_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_mpeg_parse_chunks(const QwAudioWaveChunks *chunk_view,
                                    QwAudioWaveMpegSource *source,
                                    QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWaveMpegFormat format;
    QwAudioMpegSource mpeg;
    uint64_t sample_count;
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    if (chunks.has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_wave_mpeg_format_parse(chunks.format, chunks.format_size,
                                         chunks.little_endian, &format, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.data_size) return QW_STATUS_ERR_FORMAT;

    rc = qw_audio_mpeg_parse_frames(chunks.data, chunks.data_size,
                                    QW_FORMAT_MP3, &mpeg, report);
    if (rc != QW_STATUS_OK) return rc;
    if (mpeg.sample_rate != format.sample_rate ||
        mpeg.channels != format.channels ||
        mpeg.frame_count % format.frames_per_block != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (format.codec_delay_samples >= mpeg.sample_count)
        return QW_STATUS_ERR_FORMAT;
    sample_count = mpeg.sample_count - format.codec_delay_samples;
    if (chunks.fact) {
        uint64_t fact_sample_count;
        if (chunks.fact_size < 4u) return QW_STATUS_ERR_FORMAT;
        if (!chunks.fact_sample_count_unknown) {
            fact_sample_count = qw_audio_wave_fact_sample_count(&chunks);
            if (!fact_sample_count || fact_sample_count > sample_count)
                return QW_STATUS_ERR_FORMAT;
            sample_count = fact_sample_count;
        }
    }

    source->mpeg = mpeg;
    source->sample_count = sample_count;
    source->codec_delay_samples = format.codec_delay_samples;
    source->has_metadata = chunks.has_metadata || mpeg.has_metadata;
    return QW_STATUS_OK;
}
