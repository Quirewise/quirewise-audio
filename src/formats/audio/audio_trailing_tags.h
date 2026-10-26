#ifndef QUIREWISE_AUDIO_TRAILING_TAGS_H
#define QUIREWISE_AUDIO_TRAILING_TAGS_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_trailing_tags_audio_end(const uint8_t *bytes,
                                    size_t size,
                                    size_t *audio_end);

#endif
