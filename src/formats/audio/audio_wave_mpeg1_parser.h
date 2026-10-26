#ifndef QUIREWISE_AUDIO_WAVE_MPEG1_PARSER_H
#define QUIREWISE_AUDIO_WAVE_MPEG1_PARSER_H

#include "audio_mpeg_parser.h"
#include "audio_wave_chunks.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioWaveMpeg1Source {
    QwAudioMpegSource mpeg;
    uint64_t sample_count;
    int has_metadata;
} QwAudioWaveMpeg1Source;

int qw_audio_wave_mpeg1_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioWaveMpeg1Source *source,
                              QwConvertReport *report);
int qw_audio_wave_mpeg1_parse_chunks(const QwAudioWaveChunks *chunks,
                                    QwAudioWaveMpeg1Source *source,
                                    QwConvertReport *report);

#endif
