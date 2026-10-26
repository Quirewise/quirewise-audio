#ifndef QUIREWISE_AUDIO_WAVE_MS_BLOCK_VALIDATOR_H
#define QUIREWISE_AUDIO_WAVE_MS_BLOCK_VALIDATOR_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_wave_ms_validate_blocks(const uint8_t *data,
                                     size_t size,
                                     uint16_t block_align,
                                     uint16_t channels,
                                     uint16_t coefficient_count);

#endif
