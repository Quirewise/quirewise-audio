#include "audio_custom_dsf_input.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

int qw_audio_custom_dsf_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dsf_parse(data, size, &input->dsf, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_DSF;
    input->sample_rate = input->dsf.sample_rate;
    input->channels = input->dsf.channels;
    input->sample_count = input->dsf.audio_bytes_per_channel;
    input->has_metadata = input->dsf.has_metadata;
    return 1;
}

void qw_audio_custom_dsf_input_dispose(QwAudioCustomInput *input)
{
    if (!input) return;
    free(input->dsf_packet_data);
    input->dsf_packet_data = NULL;
    input->dsf_packet_capacity = 0u;
}

int qw_audio_custom_dsf_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet)
{
    const QwAudioDsfSource *source;
    size_t block_stride;
    size_t block_offset;
    size_t packet_bytes_per_channel;
    size_t packet_size;
    size_t channel;

    if (!input || !packet || input->kind != QW_AUDIO_CUSTOM_INPUT_DSF)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    source = &input->dsf;
    if (input->packet_index >= source->block_count) return 0;
    if (!qw_checked_size_mul(source->block_size, source->channels,
                             &block_stride) ||
        !qw_checked_size_mul(input->packet_index, block_stride,
                             &block_offset)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (block_offset > source->data_size) return QW_STATUS_ERR_FORMAT;

    packet_bytes_per_channel = source->audio_bytes_per_channel -
                               input->packet_index * source->block_size;
    if (packet_bytes_per_channel > source->block_size)
        packet_bytes_per_channel = source->block_size;
    if (!packet_bytes_per_channel ||
        !qw_checked_size_mul(packet_bytes_per_channel, source->channels,
                             &packet_size)) {
        return QW_STATUS_ERR_FORMAT;
    }

    if (source->has_padded_last_block &&
        packet_bytes_per_channel < source->block_size) {
        uint8_t *grown;
        if (packet_size > input->dsf_packet_capacity) {
            grown = (uint8_t *)realloc(input->dsf_packet_data, packet_size);
            if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
            input->dsf_packet_data = grown;
            input->dsf_packet_capacity = packet_size;
        }
        for (channel = 0u; channel < source->channels; ++channel) {
            size_t source_offset = block_offset + channel * source->block_size;
            if (source_offset > source->data_size ||
                packet_bytes_per_channel > source->data_size - source_offset) {
                return QW_STATUS_ERR_FORMAT;
            }
            memcpy(input->dsf_packet_data + channel * packet_bytes_per_channel,
                   source->data + source->data_offset + source_offset,
                   packet_bytes_per_channel);
        }
        packet->data = input->dsf_packet_data;
    } else {
        if (packet_size > source->data_size - block_offset)
            return QW_STATUS_ERR_FORMAT;
        packet->data = source->data + source->data_offset + block_offset;
    }
    packet->size = packet_size;
    packet->pts = input->packet_sample_position;
    packet->duration = (uint32_t)packet_bytes_per_channel;
    input->packet_sample_position += packet_bytes_per_channel;
    ++input->packet_index;
    return 1;
}
