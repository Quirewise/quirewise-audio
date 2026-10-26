#ifndef QUIREWISE_AUDIO_CAF_AC3_PARSER_H
#define QUIREWISE_AUDIO_CAF_AC3_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafAc3Source {
    const uint8_t *data;
    size_t data_size;
    const uint8_t *packet_size_table;
    size_t packet_size_table_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    const uint8_t *extradata;
    size_t extradata_size;
    int has_metadata;
} QwAudioCafAc3Source;

typedef struct QwAudioCafAc3Frame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafAc3Frame;

int qw_audio_caf_ac3_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafAc3Source *source,
                           QwConvertReport *report);
int qw_audio_caf_ac3_next_frame(const QwAudioCafAc3Source *source,
                                size_t packet_index,
                                size_t data_offset,
                                size_t packet_size_offset,
                                QwAudioCafAc3Frame *frame);

#endif
