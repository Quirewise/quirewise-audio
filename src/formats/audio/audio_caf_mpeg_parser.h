#ifndef QUIREWISE_AUDIO_CAF_MPEG_PARSER_H
#define QUIREWISE_AUDIO_CAF_MPEG_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafMpegSource {
    const uint8_t *data;
    size_t data_size;
    const uint8_t *packet_size_table;
    size_t packet_size_table_size;
    size_t packet_count;
    size_t fixed_packet_size;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    uint8_t layer;
    int variable_packet_sizes;
    int has_metadata;
} QwAudioCafMpegSource;

typedef struct QwAudioCafMpegFrame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafMpegFrame;

int qw_audio_caf_mpeg_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafMpegSource *source,
                            QwConvertReport *report);
int qw_audio_caf_mpeg_next_frame(const QwAudioCafMpegSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafMpegFrame *frame);

#endif
