#ifndef QUIREWISE_AUDIO_CAF_IMA4_PARSER_H
#define QUIREWISE_AUDIO_CAF_IMA4_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafIma4Source {
    const uint8_t *data;
    size_t data_size;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    int has_metadata;
} QwAudioCafIma4Source;

typedef struct QwAudioCafIma4Frame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioCafIma4Frame;

int qw_audio_caf_ima4_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafIma4Source *source,
                            QwConvertReport *report);
int qw_audio_caf_ima4_next_frame(const QwAudioCafIma4Source *source,
                                 size_t packet_index,
                                 QwAudioCafIma4Frame *frame);

#endif
