#include "audio_wavpack_matroska_correction_parser.h"

#include "quirewise/status.h"

static uint32_t audio_wavpack_correction_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

int qw_audio_wavpack_matroska_correction_validate(const uint8_t *data,
                                                   size_t size,
                                                   uint16_t channels,
                                                   size_t block_count)
{
    size_t position = 0u;
    size_t index;

    if (!data || !size || !channels || !block_count)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (channels <= 2u) {
        if (block_count != 1u || size <= 4u) return QW_STATUS_ERR_FORMAT;
        (void)audio_wavpack_correction_read_u32_le(data);
        return QW_STATUS_OK;
    }

    for (index = 0u; index < block_count; ++index) {
        uint32_t correction_size;
        if (size - position < 8u) return QW_STATUS_ERR_FORMAT;
        (void)audio_wavpack_correction_read_u32_le(data + position);
        correction_size = audio_wavpack_correction_read_u32_le(
            data + position + 4u);
        position += 8u;
        if (!correction_size || (size_t)correction_size > size - position)
            return QW_STATUS_ERR_FORMAT;
        position += (size_t)correction_size;
    }
    return position == size ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}
