#ifndef QUIREWISE_AUDIO_TTA_PARSER_H
#define QUIREWISE_AUDIO_TTA_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioTtaSource {
    const uint8_t *data;
    size_t size;
    size_t frame_table_offset;
    size_t frame_data_offset;
    size_t frame_data_end;
    size_t frame_count;
    uint64_t sample_count;
    uint32_t frame_length;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t header[22];
    int has_metadata;
} QwAudioTtaSource;

typedef struct QwAudioTtaFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
} QwAudioTtaFrame;

int qw_audio_tta_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioTtaSource *source,
                       QwConvertReport *report);
int qw_audio_tta_next_frame(const QwAudioTtaSource *source,
                            size_t frame_index,
                            size_t position,
                            QwAudioTtaFrame *frame);

#endif
