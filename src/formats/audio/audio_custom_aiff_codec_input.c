#include "audio_custom_aiff_codec_input.h"

#include "audio_aiff_codec_parser.h"
#include "quirewise/status.h"

int qw_audio_custom_aiff_codec_input_parse(const uint8_t *data,
                                          size_t size,
                                          QwAudioCustomInput *input)
{
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_aiff_codec_parse(data, size, &input->aiff_codec, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_AIFF_CODEC;
    input->sample_rate = input->aiff_codec.sample_rate;
    input->channels = input->aiff_codec.channels;
    input->sample_count = input->aiff_codec.sample_count;
    input->has_metadata = input->aiff_codec.has_metadata;
    return 1;
}

int qw_audio_custom_aiff_codec_input_next_packet(
    QwAudioCustomInput *input,
    QwAudioCustomPacket *packet)
{
    QwAudioAiffCodecPacket frame;
    int rc;

    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_AIFF_CODEC)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_aiff_codec_next_packet(&input->aiff_codec,
                                         input->packet_position, &frame);
    if (rc <= 0) return rc;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->no_pts = 1u;
    input->packet_position = frame.next_position;
    return 1;
}
