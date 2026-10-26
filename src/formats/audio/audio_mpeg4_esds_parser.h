#ifndef QUIREWISE_AUDIO_MPEG4_ESDS_PARSER_H
#define QUIREWISE_AUDIO_MPEG4_ESDS_PARSER_H

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_MPEG4_ESDS_ALLOW_ATOM = 1u << 0,
    QW_AUDIO_MPEG4_ESDS_ALLOW_FULLBOX = 1u << 1,
    QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING = 1u << 2
};

int qw_audio_mpeg4_esds_find_aac_specific_info(
    const uint8_t *data,
    size_t size,
    unsigned flags,
    const uint8_t **specific_info,
    size_t *specific_info_size);

#endif
