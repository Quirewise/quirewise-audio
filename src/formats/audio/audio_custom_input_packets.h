#ifndef QUIREWISE_AUDIO_CUSTOM_INPUT_PACKETS_H
#define QUIREWISE_AUDIO_CUSTOM_INPUT_PACKETS_H

#include "audio_custom_input.h"

int qw_audio_custom_input_next_g711_data_packet(
    const uint8_t *data,
    size_t data_size,
    uint64_t sample_count,
    uint16_t channels,
    uint64_t *sample_position,
    QwAudioCustomPacket *packet);

#endif
