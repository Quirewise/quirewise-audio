#ifndef QUIREWISE_AUDIO_MPC_PARSER_INTERNAL_H
#define QUIREWISE_AUDIO_MPC_PARSER_INTERNAL_H

#include "audio_mpc_parser.h"

static inline uint32_t qw_audio_mpc_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

#endif
