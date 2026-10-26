#ifndef QUIREWISE_AUDIO_WAVE_DTS_PARSER_H
#define QUIREWISE_AUDIO_WAVE_DTS_PARSER_H

#include "audio_dts_parser.h"
#include "audio_wave_chunks.h"

typedef struct QwAudioWaveDtsSource {
    QwAudioDtsSource dts;
    uint64_t sample_count;
    int has_metadata;
} QwAudioWaveDtsSource;

int qw_audio_wave_dts_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioWaveDtsSource *source,
                            QwConvertReport *report);
int qw_audio_wave_dts_parse_chunks(const QwAudioWaveChunks *chunks,
                                   QwAudioWaveDtsSource *source,
                                   QwConvertReport *report);

#endif
