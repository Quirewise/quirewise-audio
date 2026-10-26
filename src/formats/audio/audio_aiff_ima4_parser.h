#ifndef QUIREWISE_AUDIO_AIFF_IMA4_PARSER_H
#define QUIREWISE_AUDIO_AIFF_IMA4_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAiffIma4Source {
    const uint8_t *sample_data;
    size_t sample_bytes;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    int has_metadata;
} QwAudioAiffIma4Source;

typedef struct QwAudioAiffIma4Frame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioAiffIma4Frame;

int qw_audio_aiff_ima4_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffIma4Source *source,
                             QwConvertReport *report);
int qw_audio_aiff_ima4_next_frame(const QwAudioAiffIma4Source *source,
                                  size_t packet_index,
                                  QwAudioAiffIma4Frame *frame);

#endif
