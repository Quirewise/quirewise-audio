#ifndef QUIREWISE_AUDIO_CAF_QCELP_PARSER_H
#define QUIREWISE_AUDIO_CAF_QCELP_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafQcelpSource {
    const uint8_t *data;
    size_t data_size;
    const uint8_t *packet_size_table;
    size_t packet_size_table_size;
    size_t fixed_packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    const uint8_t *extradata;
    size_t extradata_size;
    int has_metadata;
} QwAudioCafQcelpSource;

typedef struct QwAudioCafQcelpFrame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafQcelpFrame;

int qw_audio_caf_qcelp_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioCafQcelpSource *source,
                             QwConvertReport *report);
int qw_audio_caf_qcelp_next_frame(const QwAudioCafQcelpSource *source,
                                  size_t packet_index,
                                  size_t data_offset,
                                  size_t packet_size_offset,
                                  QwAudioCafQcelpFrame *frame);

#endif
