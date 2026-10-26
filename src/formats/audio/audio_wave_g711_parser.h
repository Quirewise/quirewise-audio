#ifndef QUIREWISE_AUDIO_WAVE_G711_PARSER_H
#define QUIREWISE_AUDIO_WAVE_G711_PARSER_H

#include "quirewise/convert.h"
#include "audio_wave_chunks.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioWaveG711Codec {
    QW_AUDIO_WAVE_G711_ALAW = 1,
    QW_AUDIO_WAVE_G711_MULAW = 2
} QwAudioWaveG711Codec;

typedef struct QwAudioWaveG711Source {
    const uint8_t *data;
    size_t size;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t channel_mask;
    uint16_t channels;
    QwAudioWaveG711Codec codec;
    int has_metadata;
} QwAudioWaveG711Source;

int qw_audio_wave_g711_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioWaveG711Source *source,
                             QwConvertReport *report);
int qw_audio_wave_g711_parse_chunks(const QwAudioWaveChunks *chunks,
                                    QwAudioWaveG711Source *source,
                                    QwConvertReport *report);

#endif
