#include "audio_pcm_shape.h"

#include <limits.h>

int qw_audio_pcm_rate_channels_supported(uint16_t channels,
                                         uint32_t sample_rate)
{
    return channels >= 1u && channels <= QW_AUDIO_PCM_MAX_CHANNELS &&
           sample_rate > 0u && (uint64_t)sample_rate <= (uint64_t)INT_MAX;
}

int qw_audio_pcm_shape_supported(uint16_t channels,
                                 uint32_t sample_rate,
                                 uint16_t sample_bits,
                                 int floating_point)
{
    return qw_audio_pcm_rate_channels_supported(channels, sample_rate) &&
           (floating_point
                ? (sample_bits == 32u || sample_bits == 64u)
                : (sample_bits == 8u || sample_bits == 16u ||
                   sample_bits == 24u || sample_bits == 32u));
}
