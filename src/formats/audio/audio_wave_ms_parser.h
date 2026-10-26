#ifndef QUIREWISE_AUDIO_WAVE_MS_PARSER_H
#define QUIREWISE_AUDIO_WAVE_MS_PARSER_H

#include "quirewise/convert.h"
#include "audio_wave_chunks.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveMsSource {
    const uint8_t *data;
    size_t size;
    size_t frame_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t coefficient_count;
    /* Borrowed from the input container buffer. */
    const uint8_t *coefficients;
    uint16_t channels;
    int has_standard_coefficients;
    int has_metadata;
} QwAudioWaveMsSource;

typedef struct QwAudioWaveMsFrame {
    const uint8_t *data;
    size_t size;
    size_t next_frame_index;
    uint32_t sample_count;
} QwAudioWaveMsFrame;

int qw_audio_wave_ms_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioWaveMsSource *source,
                           QwConvertReport *report);
int qw_audio_wave_ms_parse_chunks(const QwAudioWaveChunks *chunks,
                                 QwAudioWaveMsSource *source,
                                 QwConvertReport *report);
int qw_audio_wave_ms_next_frame(const QwAudioWaveMsSource *source,
                                size_t frame_index,
                                QwAudioWaveMsFrame *frame);

#endif
