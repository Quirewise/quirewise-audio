#ifndef QUIREWISE_AUDIO_WAVPACK_PARSER_H
#define QUIREWISE_AUDIO_WAVPACK_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWavpackSource {
    const uint8_t *data;
    size_t size;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t declared_total_samples;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t is_dsd;
    uint8_t version[2];
} QwAudioWavpackSource;

typedef struct QwAudioWavpackFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
    uint32_t block_index;
} QwAudioWavpackFrame;

enum {
    QW_AUDIO_WAVPACK_MAX_CHANNELS = 64u
};

int qw_audio_wavpack_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioWavpackSource *source,
                           QwConvertReport *report);
int qw_audio_wavpack_next_frame(const QwAudioWavpackSource *source,
                                size_t position,
                                QwAudioWavpackFrame *frame);

#endif
