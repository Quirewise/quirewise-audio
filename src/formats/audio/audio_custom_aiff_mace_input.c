#include "audio_custom_aiff_mace_input.h"

#include "quirewise/status.h"

int qw_audio_custom_aiff_mace_input_parse(const uint8_t *data,
                                          size_t size,
                                          QwAudioCustomInput *input)
{
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_aiff_mace_parse(data, size, &input->aiff_mace, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_AIFF_MACE;
    input->sample_rate = input->aiff_mace.sample_rate;
    input->channels = input->aiff_mace.channels;
    input->sample_count = input->aiff_mace.sample_count;
    input->has_metadata = input->aiff_mace.has_metadata;
    return 1;
}

int qw_audio_custom_aiff_mace_input_next_packet(
    QwAudioCustomInput *input,
    QwAudioCustomPacket *packet)
{
    QwAudioAiffMaceFrame frame;
    int rc;

    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_AIFF_MACE)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_aiff_mace_next_frame(&input->aiff_mace,
                                       input->packet_index, &frame);
    if (rc <= 0) return rc;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_packet_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}
