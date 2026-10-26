#include "audio_custom_dff_input.h"

#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum { AUDIO_DFF_PACKET_MAX_BYTES = 262144u };

int qw_audio_custom_dff_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dff_parse(data, size, &input->dff, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_DFF;
    input->sample_rate = input->dff.sample_rate;
    input->channels = input->dff.channels;
    input->channel_mask = input->dff.channel_mask;
    input->sample_count = input->dff.sample_count;
    input->has_metadata = input->dff.has_metadata;
    input->packet_position = input->dff.audio_data_offset;
    return 1;
}

int qw_audio_custom_dff_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    const QwAudioDffSource *source;
    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_DFF)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    source = &input->dff;

    if (source->codec == QW_AUDIO_DFF_CODEC_DSD) {
        size_t end = source->audio_data_offset + source->audio_data_size;
        size_t remaining;
        size_t packet_size;
        if (!source->channels) return QW_STATUS_ERR_FORMAT;
        if (input->packet_position >= end) {
            return input->packet_position == end ? 0 : QW_STATUS_ERR_FORMAT;
        }
        remaining = end - input->packet_position;
        packet_size = remaining < AUDIO_DFF_PACKET_MAX_BYTES
            ? remaining : AUDIO_DFF_PACKET_MAX_BYTES;
        packet_size -= packet_size % source->channels;
        if (!packet_size || packet_size > (size_t)INT_MAX)
            return QW_STATUS_ERR_FORMAT;
        packet->data = source->data + input->packet_position;
        packet->size = packet_size;
        packet->pts = input->packet_sample_position;
        packet->duration = (uint32_t)(packet_size / source->channels);
        input->packet_position += packet_size;
        input->packet_sample_position += packet->duration;
        return 1;
    }

    if (source->codec == QW_AUDIO_DFF_CODEC_DST) {
        QwAudioDffFrame frame;
        int rc = qw_audio_dff_next_dst_frame(source, input->packet_position,
                                             &frame);
        if (rc <= 0) return rc < 0 ? rc : 0;
        packet->data = frame.data;
        packet->size = frame.size;
        packet->pts = input->packet_sample_position;
        packet->duration = source->samples_per_frame;
        input->packet_position = frame.next_position;
        input->packet_sample_position += source->samples_per_frame;
        return 1;
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}
