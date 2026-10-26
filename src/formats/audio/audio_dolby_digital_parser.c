#include "audio_dolby_digital_internal.h"
#include "audio_eac3_access_unit_parser.h"

#include "quirewise/status.h"

#include <string.h>

int qw_audio_dolby_digital_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    QwAudioDolbyDigitalFrameHeader *header)
{
    uint32_t bitstream_id;
    if (!bytes || !header || size < 6u) return QW_STATUS_ERR_FORMAT;
    if (bytes[0] != 0x0bu || bytes[1] != 0x77u) return QW_STATUS_ERR_FORMAT;
    bitstream_id = (bytes[5] >> 3u) & 0x1fu;
    if (bitstream_id > 16u) return QW_STATUS_ERR_UNSUPPORTED;
    memset(header, 0, sizeof(*header));
    if (bitstream_id <= 10u) {
        return qw_audio_ac3_parse_frame_header(bytes, size, bitstream_id,
                                               header);
    }
    return qw_audio_eac3_parse_frame_header(bytes, size, bitstream_id, header);
}

int qw_audio_dolby_digital_probe(const uint8_t *bytes, size_t size)
{
    QwAudioDolbyDigitalFrameHeader header;
    return bytes &&
           qw_audio_dolby_digital_parse_frame_header(bytes, size, &header) ==
               QW_STATUS_OK &&
           header.frame_size <= size &&
           (header.codec != QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3 ||
            header.frame_type != 1u);
}

int qw_audio_dolby_digital_next_frame(
    const QwAudioDolbyDigitalSource *source,
    size_t position,
    QwAudioDolbyDigitalFrame *frame)
{
    QwAudioDolbyDigitalFrameHeader header;
    size_t remaining;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (source->codec == QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3) {
        return qw_audio_eac3_next_access_unit(source, position, frame);
    }
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;
    remaining = source->frame_data_end - position;
    rc = qw_audio_dolby_digital_parse_frame_header(
        source->data + position, remaining, &header);
    if (rc != QW_STATUS_OK) return rc;
    if (header.frame_size > remaining) return QW_STATUS_ERR_FORMAT;
    frame->data = source->data + position;
    frame->size = header.frame_size;
    frame->next_position = position + header.frame_size;
    if (header.codec != source->codec) return QW_STATUS_ERR_UNSUPPORTED;
    if (header.sample_rate != source->sample_rate ||
        header.channels != source->channels) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->sample_count = header.sample_count;
    return 1;
}

int qw_audio_dolby_digital_parse(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioDolbyDigitalSource *source,
                                 QwConvertReport *report)
{
    QwAudioDolbyDigitalFrameHeader first_header;
    size_t position = 0u;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_dolby_digital_parse_frame_header(bytes, size, &first_header);
    if (rc != QW_STATUS_OK) return rc;
    if (first_header.frame_size > size) return QW_STATUS_ERR_FORMAT;
    if (first_header.codec == QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3) {
        rc = qw_audio_eac3_select_substream(bytes, size, &first_header);
        if (rc != QW_STATUS_OK) return rc;
    }

    source->data = bytes;
    source->size = size;
    source->frame_data_end = size;
    source->sample_rate = first_header.sample_rate;
    source->channels = first_header.channels;
    source->codec = first_header.codec;
    source->substream_id = first_header.substream_id;
    while (position < source->frame_data_end) {
        QwAudioDolbyDigitalFrame frame;
        rc = qw_audio_dolby_digital_next_frame(source, position, &frame);
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
