#ifndef QUIREWISE_AUDIO_WAVE_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWavePcmFormat {
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint32_t channel_mask;
    uint16_t channels;
    uint16_t block_align;
    uint16_t sample_bits;
    int channel_layout_unspecified;
    int floating_point;
    int unsigned_8bit;
} QwAudioWavePcmFormat;

int qw_audio_wave_parse_pcm_format(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWavePcmFormat *format,
                                   QwConvertReport *report);

#endif
