#ifndef QUIREWISE_AUDIO_PCM_SHAPE_H
#define QUIREWISE_AUDIO_PCM_SHAPE_H

#include <stdint.h>

enum {
    QW_AUDIO_PCM_MAX_CHANNELS = 64
};

int qw_audio_pcm_rate_channels_supported(uint16_t channels,
                                         uint32_t sample_rate);
int qw_audio_pcm_shape_supported(uint16_t channels,
                                 uint32_t sample_rate,
                                 uint16_t sample_bits,
                                 int floating_point);

#endif
