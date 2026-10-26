#include "audio_custom_dss_input.h"

#include "quirewise/status.h"

#include <string.h>

int qw_audio_custom_dss_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dss_parse(data, size, &input->dss, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_DSS;
    input->sample_rate = input->dss.sample_rate;
    input->channels = input->dss.channels;
    input->sample_count = input->dss.sample_count;
    input->has_metadata = input->dss.has_metadata;
    return 1;
}

static int dss_next_sp_packet(QwAudioCustomInput *input,
                              QwAudioCustomPacket *packet)
{
    QwAudioDssSource *source = &input->dss;
    size_t read_size;
    size_t write_offset;
    int rc;
    if (input->packet_sample_position >= input->sample_count) return 0;

    read_size = input->dss_swap
        ? QW_AUDIO_DSS_SP_PACKET_SIZE - 2u
        : QW_AUDIO_DSS_SP_PACKET_SIZE;
    write_offset = input->dss_swap ? 3u : 0u;
    if (input->dss_payload_position > source->payload_size ||
        read_size > source->payload_size - input->dss_payload_position) {
        return 0;
    }
    memset(input->dss_packet_data, 0, sizeof(input->dss_packet_data));
    rc = qw_audio_dss_payload_read(source, input->dss_payload_position,
                                   input->dss_packet_data + write_offset,
                                   read_size);
    if (rc != QW_STATUS_OK) return rc;
    input->dss_payload_position += read_size;

    if (input->dss_swap) {
        size_t i;
        for (i = 0u; i < QW_AUDIO_DSS_SP_PACKET_SIZE - 2u; i += 2u) {
            input->dss_packet_data[i] = input->dss_packet_data[i + 4u];
        }
        input->dss_packet_data[1] = input->dss_previous_byte;
    } else {
        input->dss_previous_byte =
            input->dss_packet_data[QW_AUDIO_DSS_SP_PACKET_SIZE - 2u];
    }
    input->dss_packet_data[QW_AUDIO_DSS_SP_PACKET_SIZE - 2u] = 0u;
    input->dss_packet_data[QW_AUDIO_DSS_SP_PACKET_SIZE] = 0u;
    input->dss_swap ^= 1u;

    packet->data = input->dss_packet_data;
    packet->size = QW_AUDIO_DSS_SP_PACKET_SIZE;
    packet->pts = input->packet_sample_position;
    packet->duration = source->samples_per_frame;
    input->packet_sample_position += packet->duration;
    return 1;
}

static int dss_next_g723_packet(QwAudioCustomInput *input,
                                QwAudioCustomPacket *packet)
{
    QwAudioDssFrame frame;
    int rc;
    if (input->packet_sample_position >= input->sample_count) return 0;
    rc = qw_audio_dss_next_g723_frame(&input->dss,
                                      input->dss_payload_position, &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if (frame.size > sizeof(input->dss_packet_data)) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dss_payload_read(&input->dss,
                                   input->dss_payload_position,
                                   input->dss_packet_data, frame.size);
    if (rc != QW_STATUS_OK) return rc;
    packet->data = input->dss_packet_data;
    packet->size = frame.size;
    packet->pts = input->packet_sample_position;
    packet->duration = input->dss.samples_per_frame;
    input->dss_payload_position = frame.next_position;
    input->packet_sample_position += packet->duration;
    return 1;
}

int qw_audio_custom_dss_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_DSS) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (input->dss.codec == QW_AUDIO_DSS_CODEC_SP) {
        return dss_next_sp_packet(input, packet);
    }
    if (input->dss.codec == QW_AUDIO_DSS_CODEC_G723_1) {
        return dss_next_g723_packet(input, packet);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}
