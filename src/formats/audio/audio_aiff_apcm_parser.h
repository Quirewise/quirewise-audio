#ifndef QUIREWISE_AUDIO_AIFF_APCM_PARSER_H
#define QUIREWISE_AUDIO_AIFF_APCM_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAiffApcmChunk {
    const uint8_t *sample_data;
    size_t sample_bytes;
    uint32_t block_align;
} QwAudioAiffApcmChunk;

int qw_audio_aiff_apcm_parse_chunk(const uint8_t *body,
                                   size_t body_size,
                                   QwAudioAiffApcmChunk *chunk);

#endif
