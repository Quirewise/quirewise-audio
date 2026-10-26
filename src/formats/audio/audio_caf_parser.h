#ifndef QUIREWISE_AUDIO_CAF_PARSER_H
#define QUIREWISE_AUDIO_CAF_PARSER_H

#include "audio_pcm_parser.h"

int qw_audio_caf_parse_pcm(const uint8_t *bytes,
                           size_t size,
                           QwAudioPcmSource *source,
                           QwConvertReport *report);

#endif
