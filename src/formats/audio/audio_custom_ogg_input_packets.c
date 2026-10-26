#include "audio_custom_ogg_input.h"

#include "quirewise/status.h"

static int audio_custom_ogg_next_opus_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioOggOpusFrame frame;
    int rc = qw_audio_ogg_opus_next_frame(&input->ogg_opus,
                                          &input->ogg_packet_buffer,
                                          &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_ogg_next_flac_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioOggFlacFrame frame;
    int rc = qw_audio_ogg_flac_next_frame(&input->ogg_flac,
                                           &input->ogg_packet_buffer,
                                           &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_ogg_next_speex_packet(QwAudioCustomInput *input,
                                               QwAudioCustomPacket *packet)
{
    QwAudioOggSpeexFrame frame;
    int rc = qw_audio_ogg_speex_next_frame(&input->ogg_speex,
                                            &input->ogg_packet_buffer,
                                            &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = frame.data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_sample_position += frame.sample_count;
    return 1;
}

static int audio_custom_ogg_next_vorbis_packet(QwAudioCustomInput *input,
                                                QwAudioCustomPacket *packet)
{
    QwAudioOggVorbisFrame frame;
    int rc = qw_audio_ogg_vorbis_next_frame(
        &input->ogg_vorbis, &input->ogg_packet_buffer, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    packet->data = frame.data;
    packet->size = frame.size;
    packet->no_pts = 1u;
    return 1;
}

int qw_audio_custom_ogg_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    switch (input->kind) {
    case QW_AUDIO_CUSTOM_INPUT_OGG_OPUS:
        return audio_custom_ogg_next_opus_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_OGG_FLAC:
        return audio_custom_ogg_next_flac_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX:
        return audio_custom_ogg_next_speex_packet(input, packet);
    case QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS:
        return audio_custom_ogg_next_vorbis_packet(input, packet);
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}
