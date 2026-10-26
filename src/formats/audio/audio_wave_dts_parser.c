#include "audio_wave_dts_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_dts_format.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave_dts_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioWaveDtsSource *source,
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
    return qw_audio_wave_dts_parse_chunks(&chunks, source, report);
}

int qw_audio_wave_dts_parse_chunks(const QwAudioWaveChunks *chunks,
                                   QwAudioWaveDtsSource *source,
                                   QwConvertReport *report)
{
    QwAudioWaveDtsFormat format;
    QwAudioDtsSource dts;
    uint64_t sample_count;
    int rc;

    if (!chunks || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (chunks->has_duplicate_fact) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_wave_dts_format_parse(chunks->format, chunks->format_size,
                                        chunks->little_endian, &format,
                                        report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks->data || !chunks->data_size) return QW_STATUS_ERR_FORMAT;

    rc = qw_audio_dts_parse(chunks->data, chunks->data_size, &dts, report);
    if (rc != QW_STATUS_OK) return rc;
    if (format.sample_rate != dts.sample_rate ||
        format.channels != dts.channels) {
        return QW_STATUS_ERR_FORMAT;
    }

    sample_count = dts.sample_count;
    if (chunks->fact) {
        if (chunks->fact_size < 4u) return QW_STATUS_ERR_FORMAT;
        if (!chunks->fact_sample_count_unknown) {
            QwAudioDtsFrame frame;
            size_t position = 0u;
            uint32_t last_frame_samples = 0u;
            uint64_t fact_sample_count;

            while (position < dts.frame_data_end) {
                rc = qw_audio_dts_next_frame(&dts, position, &frame);
                if (rc <= 0 || frame.next_position <= position)
                    return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
                last_frame_samples = frame.sample_count;
                position = frame.next_position;
            }
            if (position != dts.frame_data_end || !last_frame_samples)
                return QW_STATUS_ERR_FORMAT;
            fact_sample_count = qw_audio_wave_fact_sample_count(chunks);
            if (!fact_sample_count || fact_sample_count > dts.sample_count ||
                fact_sample_count <= dts.sample_count - last_frame_samples) {
                return QW_STATUS_ERR_FORMAT;
            }
            sample_count = fact_sample_count;
        }
    }

    source->dts = dts;
    source->sample_count = sample_count;
    source->has_metadata = chunks->has_metadata;
    return QW_STATUS_OK;
}
