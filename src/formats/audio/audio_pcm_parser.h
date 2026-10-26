#ifndef QUIREWISE_AUDIO_PCM_PARSER_H
#define QUIREWISE_AUDIO_PCM_PARSER_H

#include "quirewise/convert.h"
#include "audio_wave_chunks.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioPcmSource {
    const uint8_t *samples;
    size_t sample_bytes;
    uint32_t sample_rate;
    uint64_t frames;
    uint32_t channel_mask;
    int channel_layout_unspecified;
    uint16_t channels;
    uint16_t sample_bits;
    /* Zero uses ceil(sample_bits / 8); CAF may store valid bits in a wider word. */
    uint8_t container_bytes_per_sample;
    int aligned_high;
    int little_endian;
    int floating_point;
    int unsigned_8bit;
    int has_metadata;
} QwAudioPcmSource;

int qw_audio_pcm_parse_wave(const uint8_t *bytes,
                            size_t size,
                            QwAudioPcmSource *source,
                            QwConvertReport *report);
int qw_audio_pcm_parse_wave_chunks(const QwAudioWaveChunks *chunks,
                                   QwAudioPcmSource *source,
                                   QwConvertReport *report);
int qw_audio_pcm_parse_aiff(const uint8_t *bytes,
                            size_t size,
                            QwAudioPcmSource *source,
                            QwConvertReport *report);

#endif
