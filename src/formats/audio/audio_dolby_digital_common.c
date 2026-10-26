#include "audio_dolby_digital_internal.h"

#include "quirewise/status.h"

#include <stdint.h>

static const uint32_t dolby_digital_sample_rates[3] = {
    48000u, 44100u, 32000u
};
static const uint8_t dolby_digital_channel_counts[8] = {
    2u, 1u, 2u, 3u, 3u, 4u, 4u, 5u
};

int qw_audio_dolby_digital_read_bits(const uint8_t *bytes,
                                     size_t size,
                                     size_t *bit_position,
                                     unsigned bit_count,
                                     uint32_t *value)
{
    size_t total_bits;
    uint32_t result = 0u;
    unsigned i;
    if (!bytes || !bit_position || !value || bit_count > 32u ||
        size > SIZE_MAX / 8u) {
        return QW_STATUS_ERR_FORMAT;
    }
    total_bits = size * 8u;
    if (*bit_position > total_bits || bit_count > total_bits - *bit_position)
        return QW_STATUS_ERR_FORMAT;
    for (i = 0u; i < bit_count; ++i) {
        size_t position = (*bit_position)++;
        result = (result << 1u) |
                 ((bytes[position / 8u] >> (7u - (position % 8u))) & 1u);
    }
    *value = result;
    return QW_STATUS_OK;
}

uint32_t qw_audio_dolby_digital_sample_rate(uint32_t sample_rate_code)
{
    if (sample_rate_code >=
        sizeof(dolby_digital_sample_rates) /
            sizeof(dolby_digital_sample_rates[0])) {
        return 0u;
    }
    return dolby_digital_sample_rates[sample_rate_code];
}

uint16_t qw_audio_dolby_digital_channel_count(uint32_t channel_mode,
                                              uint32_t lfe_on)
{
    if (channel_mode >=
        sizeof(dolby_digital_channel_counts) /
            sizeof(dolby_digital_channel_counts[0]) || lfe_on > 1u) {
        return 0u;
    }
    return (uint16_t)(dolby_digital_channel_counts[channel_mode] + lfe_on);
}
