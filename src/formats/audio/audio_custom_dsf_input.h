#ifndef QUIREWISE_AUDIO_CUSTOM_DSF_INPUT_H
#define QUIREWISE_AUDIO_CUSTOM_DSF_INPUT_H

#include "audio_custom_input.h"

int qw_audio_custom_dsf_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwAudioCustomInput *input);
void qw_audio_custom_dsf_input_dispose(QwAudioCustomInput *input);
int qw_audio_custom_dsf_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet);

#endif
