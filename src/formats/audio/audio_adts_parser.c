#include "audio_adts_parser.h"

#include "audio_aac_pce_parser.h"
#include "audio_id3_tag.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_adts_next_frame(const QwAudioAdtsSource *source,
                             size_t position,
                             QwAudioAdtsFrame *frame)
{
    int rc;

    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_adts_frame_parse_header(source->data,
                                          source->frame_data_end,
                                          position,
                                          frame);
    if (rc <= 0) return rc;
    if (frame->audio_object_type != source->audio_object_type ||
        frame->sampling_frequency_index != source->sampling_frequency_index ||
        frame->channel_configuration != source->channel_configuration ||
        frame->sample_rate != source->sample_rate ||
        (source->channel_configuration != 0u &&
         frame->channels != source->channels)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->channels = source->channels;
    return 1;
}

int qw_audio_adts_parse(const uint8_t *bytes,
                        size_t size,
                        QwAudioAdtsSource *source,
                        QwConvertReport *report)
{
    QwAudioAdtsFrame frame;
    size_t position;
    uint16_t channels;
    int has_trailing_id3v1;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (!qw_audio_id3_skip_leading_v2(bytes, size, &position))
        return QW_STATUS_ERR_FORMAT;
    if (size - position < 7u) return QW_STATUS_ERR_FORMAT;

    source->data = bytes;
    source->size = size;
    source->first_frame_offset = position;
    source->frame_data_end =
        qw_audio_id3v1_audio_end(bytes, size, &has_trailing_id3v1);
    source->has_metadata = position != 0u || has_trailing_id3v1;

    rc = qw_audio_adts_frame_parse_header(bytes, source->frame_data_end,
                                          position, &frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    source->sample_rate = frame.sample_rate;
    source->channels = frame.channels;
    source->audio_object_type = frame.audio_object_type;
    source->sampling_frequency_index = frame.sampling_frequency_index;
    source->channel_configuration = frame.channel_configuration;
    if (!source->channels) {
        rc = qw_audio_aac_pce_parse_first_raw_data_block(
            frame.raw_data_blocks[0].data, frame.raw_data_blocks[0].size,
            (uint8_t)(source->audio_object_type - 1u),
            source->sampling_frequency_index, &channels);
        if (rc != QW_STATUS_OK) return rc;
        source->channels = channels;
    }
    source->audio_specific_config[0] =
        (uint8_t)((source->audio_object_type << 3u) |
                  (source->sampling_frequency_index >> 1u));
    source->audio_specific_config[1] =
        (uint8_t)(((source->sampling_frequency_index & 0x01u) << 7u) |
                  (source->channel_configuration << 3u));

    position = source->first_frame_offset;
    while (position < source->frame_data_end) {
        rc = qw_audio_adts_next_frame(source, position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (source->sample_count > UINT64_MAX - frame.sample_count)
            return QW_STATUS_ERR_FORMAT;
        source->sample_count += frame.sample_count;
        position = frame.next_position;
    }
    if (position != source->frame_data_end || !source->sample_count)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
