#include "audio_custom_mpc_input.h"

#include "quirewise/status.h"

#include <string.h>

int qw_audio_custom_mpc_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_mpc_parse(data, size, &input->mpc);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_MPC;
    input->sample_rate = input->mpc.sample_rate;
    input->channels = input->mpc.channels;
    input->sample_count = input->mpc.sample_count;
    input->has_metadata = 1;
    return 1;
}

int qw_audio_custom_mpc_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    QwAudioMpcPacket mpc_packet;
    int rc;

    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_mpc_next_packet(&input->mpc, &mpc_packet);
    if (rc <= 0) return rc;
    packet->data = mpc_packet.data;
    packet->size = mpc_packet.size;
    if (mpc_packet.decoder_header_size > sizeof(packet->decoder_header))
        return QW_STATUS_ERR_FORMAT;
    if (mpc_packet.decoder_header_size) {
        memcpy(packet->decoder_header, mpc_packet.decoder_header,
               mpc_packet.decoder_header_size);
        packet->decoder_header_size = mpc_packet.decoder_header_size;
    }
    packet->duration = input->mpc.is_sv8 ? input->mpc.frame_samples : 1152u;
    if (input->mpc.is_sv8) {
        packet->no_pts = 1u;
    } else {
        if (input->packet_index > UINT64_MAX / 1152u)
            return QW_STATUS_ERR_FORMAT;
        packet->pts = (uint64_t)input->packet_index * 1152u;
        ++input->packet_index;
    }
    return 1;
}
