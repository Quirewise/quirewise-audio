#ifndef QUIREWISE_AUDIO_ADTS_FRAME_PARSER_H
#define QUIREWISE_AUDIO_ADTS_FRAME_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_ADTS_MAX_RAW_DATA_BLOCKS = 4u };

typedef struct QwAudioAdtsRawDataBlock {
    const uint8_t *data;
    size_t size;
} QwAudioAdtsRawDataBlock;

typedef struct QwAudioAdtsFrame {
    size_t next_position;
    size_t raw_data_block_count;
    QwAudioAdtsRawDataBlock raw_data_blocks[
        QW_AUDIO_ADTS_MAX_RAW_DATA_BLOCKS];
    uint32_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint8_t audio_object_type;
    uint8_t sampling_frequency_index;
    uint8_t channel_configuration;
} QwAudioAdtsFrame;

int qw_audio_adts_frame_parse_header(const uint8_t *data,
                                     size_t frame_data_end,
                                     size_t position,
                                     QwAudioAdtsFrame *frame);

#endif
