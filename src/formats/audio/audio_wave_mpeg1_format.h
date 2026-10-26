#ifndef QUIREWISE_AUDIO_WAVE_MPEG1_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_MPEG1_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveMpeg1Format {
    uint32_t sample_rate;
    uint32_t bitrate;
    uint16_t block_align;
    uint16_t channels;
    uint8_t layer;
} QwAudioWaveMpeg1Format;

/* Parses WAVEFORMATEX plus the MPEG1WAVEFORMAT extension. */
int qw_audio_wave_mpeg1_format_parse(const uint8_t *bytes,
                                     size_t size,
                                     int little_endian,
                                     QwAudioWaveMpeg1Format *format,
                                     QwConvertReport *report);

#endif
