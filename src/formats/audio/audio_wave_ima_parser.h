#ifndef QUIREWISE_AUDIO_WAVE_IMA_PARSER_H
#define QUIREWISE_AUDIO_WAVE_IMA_PARSER_H

#include "quirewise/convert.h"
#include "audio_wave_chunks.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveImaSource {
    const uint8_t *data;
    size_t size;
    size_t frame_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t channels;
    int has_metadata;
} QwAudioWaveImaSource;

typedef struct QwAudioWaveImaFrame {
    const uint8_t *data;
    size_t size;
    size_t next_frame_index;
    uint32_t sample_count;
} QwAudioWaveImaFrame;

int qw_audio_wave_ima_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioWaveImaSource *source,
                            QwConvertReport *report);
int qw_audio_wave_ima_parse_chunks(const QwAudioWaveChunks *chunks,
                                  QwAudioWaveImaSource *source,
                                  QwConvertReport *report);
int qw_audio_wave_ima_next_frame(const QwAudioWaveImaSource *source,
                                 size_t frame_index,
                                 QwAudioWaveImaFrame *frame);

#endif
