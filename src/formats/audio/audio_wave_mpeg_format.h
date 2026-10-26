#ifndef QUIREWISE_AUDIO_WAVE_MPEG_FORMAT_H
#define QUIREWISE_AUDIO_WAVE_MPEG_FORMAT_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveMpegFormat {
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    uint32_t flags;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t block_size;
    uint16_t frames_per_block;
    uint16_t codec_delay_samples;
} QwAudioWaveMpegFormat;

/* Parses the WAVEFORMATEX and MPEGLAYER3WAVEFORMAT description only. */
int qw_audio_wave_mpeg_format_parse(const uint8_t *bytes,
                                    size_t size,
                                    int little_endian,
                                    QwAudioWaveMpegFormat *format,
                                    QwConvertReport *report);

#endif
