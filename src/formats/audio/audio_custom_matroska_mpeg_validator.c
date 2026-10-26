#include "audio_custom_matroska_mpeg_validator.h"

#include "audio_mpeg_parser.h"
#include "quirewise/status.h"

static uint8_t matroska_mpeg_layer(QwAudioMatroskaCodec codec)
{
    switch (codec) {
    case QW_AUDIO_MATROSKA_CODEC_MP1: return 1u;
    case QW_AUDIO_MATROSKA_CODEC_MP2: return 2u;
    case QW_AUDIO_MATROSKA_CODEC_MP3: return 3u;
    default: return 0u;
    }
}

int qw_audio_custom_matroska_mpeg_validate_packet(
        const QwAudioMatroskaSource *source,
        const uint8_t *bytes,
        size_t size)
{
    QwAudioMpegSource mpeg;
    uint8_t expected_layer;
    int rc;

    if (!source || !bytes) return QW_STATUS_ERR_INVALID_ARGUMENT;
    expected_layer = matroska_mpeg_layer(source->codec);
    if (!expected_layer) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!size) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_mpeg_parse_layer_frames(bytes, size, expected_layer,
                                          &mpeg, NULL);
    if (rc != QW_STATUS_OK) return rc;
    if (mpeg.sample_rate != (source->coded_sample_rate
                                 ? source->coded_sample_rate
                                 : source->sample_rate) ||
        mpeg.channels != source->channels) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (source->is_mpeg1_acm_format) {
        size_t position = mpeg.first_frame_offset;
        if (mpeg.version_id != 3u) return QW_STATUS_ERR_FORMAT;
        while (position < mpeg.frame_data_end) {
            QwAudioMpegFrame frame;
            rc = qw_audio_mpeg_next_frame(&mpeg, position, &frame);
            if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
            if (frame.size < 2u ||
                ((frame.data[1] >> 3u) & 0x03u) != 3u) {
                return QW_STATUS_ERR_FORMAT;
            }
            if ((source->mpeg1_bitrate &&
                 frame.bitrate != source->mpeg1_bitrate) ||
                (source->block_align != 1u &&
                 frame.size != (size_t)source->block_align)) {
                return QW_STATUS_ERR_FORMAT;
            }
            position = frame.next_position;
        }
        if (position != mpeg.frame_data_end) return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}
