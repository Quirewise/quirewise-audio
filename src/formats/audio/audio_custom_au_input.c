#include "audio_custom_au_input.h"

#include "audio_au_adpcm_parser.h"
#include "audio_au_parser.h"
#include "quirewise/status.h"

enum {
    AUDIO_AU_PACKET_MAX_SIZE = 64u * 1024u
};

int qw_audio_custom_au_input_parse(const uint8_t *data,
                                   size_t size,
                                   QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_au_parse(data, size, &input->au, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;

    if (input->au.encoding == QW_AUDIO_AU_ENCODING_ULAW) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_AU_ULAW;
    } else if (input->au.encoding == QW_AUDIO_AU_ENCODING_ALAW) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_AU_ALAW;
    } else if (qw_audio_au_adpcm_describe(input->au.encoding, NULL)) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_AU_ADPCM;
    } else {
        return 0;
    }
    input->sample_rate = input->au.sample_rate;
    input->channels = input->au.channels;
    input->sample_count = input->au.frames;
    input->has_metadata = input->au.has_metadata;
    return 1;
}

static size_t audio_au_packet_size(size_t remaining, size_t alignment)
{
    size_t packet_size = remaining > AUDIO_AU_PACKET_MAX_SIZE
        ? AUDIO_AU_PACKET_MAX_SIZE : remaining;
    if (remaining > AUDIO_AU_PACKET_MAX_SIZE && alignment > 1u)
        packet_size -= packet_size % alignment;
    return packet_size;
}

static int audio_custom_au_next_pcm_packet(QwAudioCustomInput *input,
                                           QwAudioCustomPacket *packet)
{
    size_t remaining;
    size_t packet_size;
    uint32_t packet_samples;
    if (input->packet_position >= input->au.sample_bytes) return 0;
    if (!input->channels) return QW_STATUS_ERR_FORMAT;
    remaining = input->au.sample_bytes - input->packet_position;
    packet_size = audio_au_packet_size(remaining, input->channels);
    if (!packet_size || packet_size % input->channels)
        return QW_STATUS_ERR_FORMAT;
    packet_samples = (uint32_t)(packet_size / input->channels);
    if (input->packet_sample_position > UINT64_MAX - packet_samples)
        return QW_STATUS_ERR_FORMAT;

    packet->data = input->au.samples + input->packet_position;
    packet->size = packet_size;
    packet->pts = input->packet_sample_position;
    packet->duration = packet_samples;
    input->packet_position += packet_size;
    input->packet_sample_position += packet_samples;
    return 1;
}

static int audio_custom_au_next_adpcm_packet(QwAudioCustomInput *input,
                                             QwAudioCustomPacket *packet)
{
    QwAudioAuAdpcmInfo adpcm_info;
    size_t remaining;
    size_t packet_size;
    uint32_t packet_samples;
    int rc;
    if (input->packet_position >= input->au.sample_bytes) return 0;
    if (!qw_audio_au_adpcm_describe(input->au.encoding, &adpcm_info))
        return QW_STATUS_ERR_FORMAT;
    remaining = input->au.sample_bytes - input->packet_position;
    packet_size = audio_au_packet_size(remaining,
                                       adpcm_info.packet_alignment);
    if (!packet_size) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_au_adpcm_packet_sample_count(
        &adpcm_info, packet_size, &packet_samples);
    if (rc != QW_STATUS_OK) return rc;
    if (input->packet_sample_position > UINT64_MAX - packet_samples)
        return QW_STATUS_ERR_FORMAT;

    packet->data = input->au.samples + input->packet_position;
    packet->size = packet_size;
    packet->pts = input->packet_sample_position;
    packet->duration = packet_samples;
    input->packet_position += packet_size;
    input->packet_sample_position += packet_samples;
    return 1;
}

int qw_audio_custom_au_input_next_packet(QwAudioCustomInput *input,
                                         QwAudioCustomPacket *packet)
{
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (input->kind == QW_AUDIO_CUSTOM_INPUT_AU_ADPCM)
        return audio_custom_au_next_adpcm_packet(input, packet);
    if (input->kind == QW_AUDIO_CUSTOM_INPUT_AU_ULAW ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_AU_ALAW) {
        return audio_custom_au_next_pcm_packet(input, packet);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}
