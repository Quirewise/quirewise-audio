#ifndef QUIREWISE_AUDIO_WAVE_MPEG_PARSER_H
#define QUIREWISE_AUDIO_WAVE_MPEG_PARSER_H

#include "audio_mpeg_parser.h"
#include "audio_wave_chunks.h"

typedef struct QwAudioWaveMpegSource {
    QwAudioMpegSource mpeg;
    uint64_t sample_count;
    uint16_t codec_delay_samples;
    int has_metadata;
} QwAudioWaveMpegSource;

int qw_audio_wave_mpeg_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioWaveMpegSource *source,
                             QwConvertReport *report);
int qw_audio_wave_mpeg_parse_chunks(const QwAudioWaveChunks *chunks,
                                    QwAudioWaveMpegSource *source,
                                    QwConvertReport *report);

#endif
