#include "audio_wave_mpeg1_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_mpeg1_format.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave_mpeg1_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioWaveMpeg1Source *source,
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
    return qw_audio_wave_mpeg1_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_mpeg1_parse_chunks(const QwAudioWaveChunks *chunk_view,
                                    QwAudioWaveMpeg1Source *source,
                                    QwConvertReport *report)
{
    QwAudioWaveChunks chunks;
    QwAudioWaveMpeg1Format format;
    QwAudioMpegSource mpeg;
    uint64_t sample_count;
    size_t position;
    int rc;

    if (!chunk_view || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    chunks = *chunk_view;
    if (chunks.has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_wave_mpeg1_format_parse(chunks.format,
                                         chunks.format_size,
                                         chunks.little_endian,
                                         &format, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.data || !chunks.data_size) return QW_STATUS_ERR_FORMAT;

    rc = qw_audio_mpeg_parse_layer_frames(chunks.data, chunks.data_size,
                                          format.layer, &mpeg, report);
    if (rc != QW_STATUS_OK) return rc;
    if (mpeg.version_id != 3u) return QW_STATUS_ERR_UNSUPPORTED;
    if ((format.sample_rate && mpeg.sample_rate != format.sample_rate) ||
        mpeg.channels != format.channels) {
        return QW_STATUS_ERR_FORMAT;
    }

    position = mpeg.first_frame_offset;
    while (position < mpeg.frame_data_end) {
        QwAudioMpegFrame frame;
        rc = qw_audio_mpeg_next_frame(&mpeg, position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (frame.size < 2u || ((frame.data[1] >> 3u) & 0x03u) != 3u)
            return QW_STATUS_ERR_FORMAT;
        if ((format.bitrate && frame.bitrate != format.bitrate) ||
            (format.block_align != 1u &&
             frame.size != (size_t)format.block_align)) {
            return QW_STATUS_ERR_FORMAT;
        }
        position = frame.next_position;
    }
    if (position != mpeg.frame_data_end) return QW_STATUS_ERR_FORMAT;

    sample_count = mpeg.sample_count;
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
    source->has_metadata = chunks.has_metadata || mpeg.has_metadata;
    return QW_STATUS_OK;
}
