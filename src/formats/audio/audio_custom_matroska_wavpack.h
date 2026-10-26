#ifndef QUIREWISE_AUDIO_CUSTOM_MATROSKA_WAVPACK_H
#define QUIREWISE_AUDIO_CUSTOM_MATROSKA_WAVPACK_H

#include "audio_matroska_parser.h"

#include <stddef.h>
#include <stdint.h>

int qw_audio_custom_matroska_wavpack_validate(
        const QwAudioMatroskaSource *source);

int qw_audio_custom_matroska_wavpack_reconstruct_packet(
        const QwAudioMatroskaSource *source,
        const uint8_t *packet_data,
        size_t packet_size,
        uint8_t **buffer,
        size_t *buffer_capacity,
        const uint8_t **reconstructed_data,
        size_t *reconstructed_size);

#endif
