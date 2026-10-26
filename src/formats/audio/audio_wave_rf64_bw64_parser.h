#ifndef QUIREWISE_AUDIO_WAVE_RF64_BW64_PARSER_H
#define QUIREWISE_AUDIO_WAVE_RF64_BW64_PARSER_H

#include "audio_wave_chunks.h"

int qw_audio_wave_rf64_bw64_parse_chunks(const uint8_t *bytes,
                                         size_t size,
                                         QwAudioWaveChunks *chunks,
                                         QwConvertReport *report);

#endif
