#ifndef QUIREWISE_AUDIO_CAF_ILBC_PARSER_H
#define QUIREWISE_AUDIO_CAF_ILBC_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafIlbcSource {
    const uint8_t *data;
    size_t data_size;
    const uint8_t *packet_size_table;
    size_t packet_size_table_size;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    int variable_packet_sizes;
    int has_metadata;
} QwAudioCafIlbcSource;

typedef struct QwAudioCafIlbcFrame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafIlbcFrame;

int qw_audio_caf_ilbc_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafIlbcSource *source,
                            QwConvertReport *report);
int qw_audio_caf_ilbc_next_frame(const QwAudioCafIlbcSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafIlbcFrame *frame);

#endif
