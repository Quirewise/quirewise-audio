#ifndef QUIREWISE_AUDIO_CAF_ALAC_PARSER_H
#define QUIREWISE_AUDIO_CAF_ALAC_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE = 36u };

typedef struct QwAudioCafAlacSource {
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
    uint32_t max_frame_bytes;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t extradata[QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE];
    int variable_packet_sizes;
    int has_metadata;
} QwAudioCafAlacSource;

typedef struct QwAudioCafAlacFrame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafAlacFrame;

int qw_audio_caf_alac_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafAlacSource *source,
                            QwConvertReport *report);
int qw_audio_caf_alac_next_frame(const QwAudioCafAlacSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafAlacFrame *frame);

#endif
