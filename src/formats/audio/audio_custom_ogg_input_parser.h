#ifndef QUIREWISE_AUDIO_CUSTOM_OGG_INPUT_PARSER_H
#define QUIREWISE_AUDIO_CUSTOM_OGG_INPUT_PARSER_H

#include "audio_custom_input.h"

int qw_audio_custom_ogg_input_parse_selected(const uint8_t *data,
                                             size_t size,
                                             QwAudioCustomInput *input);

#endif
