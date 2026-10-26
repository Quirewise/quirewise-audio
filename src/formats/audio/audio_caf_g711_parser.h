#ifndef QUIREWISE_AUDIO_CAF_G711_PARSER_H
#define QUIREWISE_AUDIO_CAF_G711_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioCafG711Codec {
    QW_AUDIO_CAF_G711_ALAW = 1,
    QW_AUDIO_CAF_G711_MULAW = 2
} QwAudioCafG711Codec;

typedef struct QwAudioCafG711Source {
    const uint8_t *data;
    size_t size;
    uint64_t encoded_frame_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    QwAudioCafG711Codec codec;
    int has_metadata;
} QwAudioCafG711Source;

int qw_audio_caf_g711_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafG711Source *source,
                            QwConvertReport *report);

#endif
