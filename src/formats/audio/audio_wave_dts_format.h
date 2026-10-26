#ifndef QUIREWISE_AUDIO_WAVE_DTS_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_DTS_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveDtsFormat {
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t extension_size;
} QwAudioWaveDtsFormat;

/* Parses only the WAVE format tag and WAVEFORMATEX descriptor bounds. */
int qw_audio_wave_dts_format_parse(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWaveDtsFormat *format,
                                   QwConvertReport *report);

#endif
