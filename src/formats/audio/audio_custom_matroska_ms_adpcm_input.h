#ifndef QUIREWISE_AUDIO_CUSTOM_MATROSKA_MS_ADPCM_INPUT_H
#define QUIREWISE_AUDIO_CUSTOM_MATROSKA_MS_ADPCM_INPUT_H

#include "audio_custom_input.h"

int qw_audio_custom_matroska_ms_adpcm_decode_packet(
    QwAudioCustomInput *input,
    const uint8_t *data,
    size_t size,
    QwAudioCustomPacket *packet);

#endif
