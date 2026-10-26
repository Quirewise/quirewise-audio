#ifndef QUIREWISE_AUDIO_CUSTOM_PCM_INPUT_H
#define QUIREWISE_AUDIO_CUSTOM_PCM_INPUT_H

#include "audio_custom_input.h"

int qw_audio_custom_pcm_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwInputFormat input_format,
                                    QwAudioCustomInput *input);

#endif
