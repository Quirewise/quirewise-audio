#ifndef QUIREWISE_AUDIO_AIFF_CHUNKS_H
#define QUIREWISE_AUDIO_AIFF_CHUNKS_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAiffChunks {
    const uint8_t *sample_data;
    size_t sample_bytes;
    uint32_t frame_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t sample_bits;
    const uint8_t *codec_extradata;
    size_t codec_extradata_size;
    uint32_t apcm_block_align;
    uint8_t compression_type[4];
    int aifc;
    int has_apcm;
    int has_codec_extradata;
    int has_metadata;
} QwAudioAiffChunks;

int qw_audio_aiff_parse_chunks(const uint8_t *bytes,
                               size_t size,
                               QwAudioAiffChunks *chunks,
                               QwConvertReport *report);

#endif
