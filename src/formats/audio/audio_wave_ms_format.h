#ifndef QUIREWISE_AUDIO_WAVE_MS_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_MS_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveMsFormat {
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t extension_size;
    uint16_t coefficient_count;
    /* Borrowed from the WAVEFORMATEX bytes passed to the parser. */
    const uint8_t *coefficients;
    int has_standard_coefficients;
} QwAudioWaveMsFormat;

/* Parses WAVEFORMATEX metadata and borrows its coefficient table from bytes. */
int qw_audio_wave_ms_format_parse(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioWaveMsFormat *format,
                                 QwConvertReport *report);

#endif
