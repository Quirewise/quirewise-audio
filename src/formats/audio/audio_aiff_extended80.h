#ifndef QUIREWISE_AUDIO_AIFF_EXTENDED80_H
#define QUIREWISE_AUDIO_AIFF_EXTENDED80_H

#include <stdint.h>

int qw_audio_aiff_extended80_to_rate(const uint8_t bytes[10],
                                    uint32_t *rate_out);

#endif
