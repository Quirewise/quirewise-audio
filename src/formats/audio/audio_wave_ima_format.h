#ifndef QUIREWISE_AUDIO_WAVE_IMA_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_IMA_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveImaFormat {
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t extension_size;
} QwAudioWaveImaFormat;

/* Parses the WAVEFORMATEX payload for Microsoft IMA ADPCM. */
int qw_audio_wave_ima_format_parse(const uint8_t *bytes,
                                  size_t size,
                                  QwAudioWaveImaFormat *format,
                                  QwConvertReport *report);

/* Validates complete IMA ADPCM blocks without interpreting a container. */
int qw_audio_wave_ima_validate_blocks(const uint8_t *data,
                                      size_t size,
                                      uint16_t block_align,
                                      uint16_t channels);

#endif
