#ifndef QUIREWISE_AUDIO_AIFF_G711_PARSER_H
#define QUIREWISE_AUDIO_AIFF_G711_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioAiffG711Codec {
    QW_AUDIO_AIFF_G711_ALAW = 1,
    QW_AUDIO_AIFF_G711_MULAW = 2
} QwAudioAiffG711Codec;

typedef struct QwAudioAiffG711Source {
    const uint8_t *data;
    size_t size;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    QwAudioAiffG711Codec codec;
    int has_metadata;
} QwAudioAiffG711Source;

int qw_audio_aiff_g711_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffG711Source *source,
                             QwConvertReport *report);

#endif
