#ifndef QUIREWISE_AUDIO_WAVE_CHUNKS_H
#define QUIREWISE_AUDIO_WAVE_CHUNKS_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveChunks {
    const uint8_t *format;
    size_t format_size;
    const uint8_t *data;
    size_t data_size;
    const uint8_t *fact;
    size_t fact_size;
    uint64_t fact_sample_count;
    int little_endian;
    int has_duplicate_fact;
    int has_fact_sample_count;
    int fact_sample_count_unknown;
    int has_metadata;
} QwAudioWaveChunks;

int qw_audio_wave_parse_chunks(const uint8_t *bytes,
                               size_t size,
                               QwAudioWaveChunks *chunks,
                               QwConvertReport *report);

uint64_t qw_audio_wave_fact_sample_count(const QwAudioWaveChunks *chunks);

int qw_audio_wave64_parse_chunks(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioWaveChunks *chunks,
                                 QwConvertReport *report);

#endif
