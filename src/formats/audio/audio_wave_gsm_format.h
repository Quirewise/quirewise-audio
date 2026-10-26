#ifndef QUIREWISE_AUDIO_WAVE_GSM_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_GSM_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveGsmFormat {
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t extension_size;
} QwAudioWaveGsmFormat;

/* Parses only the WAVEFORMATEX and GSM610WAVEFORMAT fields. */
int qw_audio_wave_gsm_format_parse(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWaveGsmFormat *format,
                                   QwConvertReport *report);

#endif
