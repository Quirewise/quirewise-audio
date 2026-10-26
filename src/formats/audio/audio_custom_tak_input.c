#include "audio_custom_input.h"

#include "quirewise/status.h"

int qw_audio_custom_tak_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_tak_parse(data, size, &input->tak);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_TAK;
    input->sample_rate = input->tak.sample_rate;
    input->channels = input->tak.channels;
    input->channel_mask = input->tak.channel_mask;
    input->sample_count = input->tak.sample_count;
    input->packet_position = input->tak.audio_data_offset;
    input->has_metadata = 1;
    return 1;
}

int qw_audio_custom_tak_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    QwAudioTakFrame frame;
    uint32_t duration;
    int rc;
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (input->tak.sample_count &&
        input->packet_sample_position >= input->tak.sample_count) {
        return input->packet_position == input->tak.audio_data_end
            ? 0 : QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_tak_next_frame(&input->tak, input->packet_position, &frame);
    if (rc <= 0) {
        if (rc < 0) return rc;
        return input->tak.sample_count &&
               input->packet_sample_position != input->tak.sample_count
            ? QW_STATUS_ERR_FORMAT : 0;
    }
    duration = frame.sample_count;
    if (input->tak.sample_count) {
        uint64_t remaining = input->tak.sample_count -
                             input->packet_sample_position;
        if (remaining < duration) duration = (uint32_t)remaining;
    }
    if (!duration || (uint64_t)duration >
            UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = duration;
    input->packet_position = frame.next_position;
    input->packet_sample_position += duration;
    ++input->packet_index;
    return 1;
}
