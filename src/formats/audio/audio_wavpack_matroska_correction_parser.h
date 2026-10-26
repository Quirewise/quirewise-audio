#ifndef QUIREWISE_AUDIO_WAVPACK_MATROSKA_CORRECTION_PARSER_H
#define QUIREWISE_AUDIO_WAVPACK_MATROSKA_CORRECTION_PARSER_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_wavpack_matroska_correction_validate(const uint8_t *data,
                                                   size_t size,
                                                   uint16_t channels,
                                                   size_t block_count);

#endif
