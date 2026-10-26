#include "audio_eac3_access_unit_parser.h"

#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

int qw_audio_eac3_select_substream(
    const uint8_t *bytes,
    size_t size,
    QwAudioDolbyDigitalFrameHeader *selected_header)
{
    QwAudioDolbyDigitalFrameHeader first_header;
    size_t position = 0u;
    int rc;

    if (!bytes || !selected_header) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dolby_digital_parse_frame_header(bytes, size, &first_header);
    if (rc != QW_STATUS_OK) return rc;
    if (first_header.codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (first_header.frame_size > size) return QW_STATUS_ERR_FORMAT;
    if (first_header.frame_type == 1u) return QW_STATUS_ERR_UNSUPPORTED;

    /* The current decoder route accepts independent substream 0 only. */
    if (first_header.substream_id == 0u) {
        *selected_header = first_header;
        return QW_STATUS_OK;
    }

    while (position < size) {
        QwAudioDolbyDigitalFrameHeader candidate;

        rc = qw_audio_dolby_digital_parse_frame_header(
            bytes + position, size - position, &candidate);
        if (rc != QW_STATUS_OK) return rc;
        if (candidate.frame_size > size - position) return QW_STATUS_ERR_FORMAT;
        if (candidate.codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (candidate.frame_type != 1u && candidate.substream_id == 0u) {
            *selected_header = candidate;
            return QW_STATUS_OK;
        }
        position += candidate.frame_size;
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

int qw_audio_eac3_next_access_unit(
    const QwAudioDolbyDigitalSource *source,
    size_t position,
    QwAudioDolbyDigitalFrame *frame)
{
    QwAudioDolbyDigitalFrameHeader header;
    size_t remaining;
    size_t next_position;
    int rc;

    if (!source || !frame || !source->data ||
        source->codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;

    remaining = source->frame_data_end - position;
    rc = qw_audio_dolby_digital_parse_frame_header(
        source->data + position, remaining, &header);
    if (rc != QW_STATUS_OK) return rc;
    if (header.codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (header.frame_type == 1u) return QW_STATUS_ERR_UNSUPPORTED;
    if (header.frame_size > remaining) return QW_STATUS_ERR_FORMAT;

    frame->data = source->data + position;
    frame->size = header.frame_size;
    next_position = position + header.frame_size;
    frame->next_position = next_position;

    while (next_position < source->frame_data_end) {
        QwAudioDolbyDigitalFrameHeader dependent;
        size_t group_size;

        rc = qw_audio_dolby_digital_parse_frame_header(
            source->data + next_position,
            source->frame_data_end - next_position, &dependent);
        if (rc != QW_STATUS_OK) return rc;
        if (dependent.codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (dependent.frame_type != 1u) break;
        if (dependent.frame_size >
            source->frame_data_end - next_position) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (dependent.substream_id != header.substream_id ||
            dependent.sample_rate != header.sample_rate ||
            dependent.sample_count != header.sample_count) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        group_size = frame->size;
        if ((size_t)dependent.frame_size > SIZE_MAX - group_size ||
            group_size + (size_t)dependent.frame_size > (size_t)INT_MAX) {
            return QW_STATUS_ERR_RESOURCE_LIMIT;
        }
        frame->size = group_size + (size_t)dependent.frame_size;
        next_position += dependent.frame_size;
        frame->next_position = next_position;
    }

    if (header.substream_id != source->substream_id) return 1;
    if (header.sample_rate != source->sample_rate ||
        header.channels != source->channels) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->sample_count = header.sample_count;
    return 1;
}
