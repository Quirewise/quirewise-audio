#ifndef QUIREWISE_AUDIO_AMR_PARSER_H
#define QUIREWISE_AUDIO_AMR_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAmrSource {
    const uint8_t *data;
    size_t size;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    int wideband;
} QwAudioAmrSource;

typedef struct QwAudioAmrFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
} QwAudioAmrFrame;

int qw_audio_amr_probe(const uint8_t *bytes, size_t size);
int qw_audio_amr_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioAmrSource *source,
                       QwConvertReport *report);
int qw_audio_amr_next_frame(const QwAudioAmrSource *source,
                            size_t position,
                            QwAudioAmrFrame *frame);

#endif
