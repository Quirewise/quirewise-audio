#ifndef QUIREWISE_AUDIO_WAVE_SUBFORMAT_H
#define QUIREWISE_AUDIO_WAVE_SUBFORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveSubformat {
    uint16_t format_tag;
    uint16_t sample_bits;
    uint16_t valid_bits;
    uint32_t channel_mask;
    int extensible;
} QwAudioWaveSubformat;

int qw_audio_wave_parse_subformat(const uint8_t *bytes,
                                  size_t size,
                                  int little_endian,
                                  QwAudioWaveSubformat *subformat,
                                  QwConvertReport *report);

#endif
