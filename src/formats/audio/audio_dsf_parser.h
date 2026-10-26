#ifndef QUIREWISE_AUDIO_DSF_PARSER_H
#define QUIREWISE_AUDIO_DSF_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioDsfSource {
    const uint8_t *data;
    size_t size;
    size_t data_offset;
    size_t data_size;
    size_t audio_bytes_per_channel;
    size_t block_size;
    size_t block_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t channel_type;
    uint16_t channels;
    uint8_t bits_per_sample;
    int has_padded_last_block;
    int has_metadata;
} QwAudioDsfSource;

int qw_audio_dsf_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDsfSource *source,
                       QwConvertReport *report);

#endif
