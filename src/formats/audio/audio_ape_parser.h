#ifndef QUIREWISE_AUDIO_APE_PARSER_H
#define QUIREWISE_AUDIO_APE_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioApeSource {
    const uint8_t *data;
    size_t size;
    size_t stream_offset;
    size_t seek_table_offset;
    size_t bit_table_offset;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t blocks_per_frame;
    uint32_t final_frame_blocks;
    uint32_t frame_count;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t version;
    uint16_t compression_type;
    uint16_t format_flags;
    uint8_t decoder_extradata[6];
    int has_metadata;
} QwAudioApeSource;

typedef struct QwAudioApeFrame {
    const uint8_t *data;
    size_t size;
    uint32_t sample_count;
    uint32_t skip;
} QwAudioApeFrame;

int qw_audio_ape_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioApeSource *source,
                       QwConvertReport *report);
int qw_audio_ape_next_frame(const QwAudioApeSource *source,
                            size_t frame_index,
                            QwAudioApeFrame *frame);

#endif
