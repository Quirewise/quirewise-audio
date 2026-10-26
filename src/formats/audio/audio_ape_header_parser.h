#ifndef QUIREWISE_AUDIO_APE_HEADER_PARSER_H
#define QUIREWISE_AUDIO_APE_HEADER_PARSER_H

#include "audio_ape_parser.h"

int qw_audio_ape_header_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioApeSource *source,
                              QwConvertReport *report);

#endif
