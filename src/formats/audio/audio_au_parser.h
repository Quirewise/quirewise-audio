#ifndef QUIREWISE_AUDIO_AU_PARSER_H
#define QUIREWISE_AUDIO_AU_PARSER_H

#include "audio_pcm_parser.h"

typedef enum QwAudioAuEncoding {
    QW_AUDIO_AU_ENCODING_ULAW = 1u,
    QW_AUDIO_AU_ENCODING_LINEAR_8 = 2u,
    QW_AUDIO_AU_ENCODING_LINEAR_16 = 3u,
    QW_AUDIO_AU_ENCODING_LINEAR_24 = 4u,
    QW_AUDIO_AU_ENCODING_LINEAR_32 = 5u,
    QW_AUDIO_AU_ENCODING_FLOAT = 6u,
    QW_AUDIO_AU_ENCODING_DOUBLE = 7u,
    QW_AUDIO_AU_ENCODING_G721_32 = 23u,
    QW_AUDIO_AU_ENCODING_G722 = 24u,
    QW_AUDIO_AU_ENCODING_G723_24 = 25u,
    QW_AUDIO_AU_ENCODING_G723_40 = 26u,
    QW_AUDIO_AU_ENCODING_ALAW = 27u,
    QW_AUDIO_AU_ENCODING_G726_16 = UINT32_C(0x37323632)
} QwAudioAuEncoding;

typedef struct QwAudioAuSource {
    const uint8_t *samples;
    size_t sample_bytes;
    uint32_t sample_rate;
    uint64_t frames;
    uint32_t encoding;
    uint16_t channels;
    int has_metadata;
} QwAudioAuSource;

int qw_audio_au_parse(const uint8_t *bytes,
                      size_t size,
                      QwAudioAuSource *source,
                      QwConvertReport *report);

int qw_audio_au_parse_pcm(const uint8_t *bytes,
                          size_t size,
                          QwAudioPcmSource *source,
                          QwConvertReport *report);

#endif
