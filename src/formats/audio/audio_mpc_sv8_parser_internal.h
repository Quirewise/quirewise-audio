#ifndef QUIREWISE_AUDIO_MPC_SV8_PARSER_INTERNAL_H
#define QUIREWISE_AUDIO_MPC_SV8_PARSER_INTERNAL_H

#include "audio_mpc_parser.h"

int qw_audio_mpc_sv8_parse(const uint8_t *data,
                           size_t size,
                           QwAudioMpcSource *source);
int qw_audio_mpc_sv8_next_packet(QwAudioMpcSource *source,
                                 QwAudioMpcPacket *packet);

#endif
