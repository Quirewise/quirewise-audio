#ifndef QUIREWISE_AUDIO_CUSTOM_MP4_INPUT_H
#define QUIREWISE_AUDIO_CUSTOM_MP4_INPUT_H

#include "audio_custom_input.h"

int qw_audio_custom_mp4_input_parse(const uint8_t *data,
                                    size_t size,
                                    size_t requested_audio_track,
                                    QwAudioCustomInput *input);
int qw_audio_custom_mp4_input_next_packet(QwAudioCustomInput *input,
                                          QwAudioCustomPacket *packet);

#endif
