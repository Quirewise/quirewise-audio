#include "audio_wave_ms_block_validator.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>

int qw_audio_wave_ms_validate_blocks(const uint8_t *data,
                                     size_t size,
                                     uint16_t block_align,
                                     uint16_t channels,
                                     uint16_t coefficient_count)
{
    size_t block_count;
    size_t block_index;

    if (!data || !size || !block_align || !channels || channels > 6u ||
        !coefficient_count || coefficient_count > 256u) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (block_align < (uint16_t)(channels * 7u) ||
        size % block_align != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    block_count = size / block_align;
    for (block_index = 0u; block_index < block_count; ++block_index) {
        const uint8_t *block = data + block_index * (size_t)block_align;
        size_t channel;
        for (channel = 0u; channel < channels; ++channel) {
            size_t delta_offset = (size_t)channels + channel * 2u;
            uint16_t initial_delta = qw_read_u16_le(block + delta_offset);
            if ((uint16_t)block[channel] >= coefficient_count ||
                initial_delta < 16u || initial_delta > INT16_MAX) {
                return QW_STATUS_ERR_FORMAT;
            }
        }
    }
    return QW_STATUS_OK;
}
