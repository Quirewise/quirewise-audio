#ifndef QUIREWISE_AUDIO_CAF_VARINT_H
#define QUIREWISE_AUDIO_CAF_VARINT_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_caf_varint_read(const uint8_t *bytes,
                             size_t size,
                             size_t *position,
                             uint64_t *value);

#endif
