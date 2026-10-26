#include "audio_dts_exss_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    DTS_EXSS_SYNCWORD = 0x64582025u,
    DTS_EXSS_MIN_HEADER_SIZE = 10u
};

int qw_audio_dts_exss_frame_size(const uint8_t *data,
                                 size_t size,
                                 size_t *frame_size)
{
    uint64_t header_window = 0u;
    uint32_t encoded_size;
    uint32_t syncword;
    size_t index;

    if (!data || !frame_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *frame_size = 0u;
    if (size < 4u) return QW_STATUS_ERR_FORMAT;
    syncword = ((uint32_t)data[0] << 24u) |
               ((uint32_t)data[1] << 16u) |
               ((uint32_t)data[2] << 8u) |
               (uint32_t)data[3];
    if (syncword != DTS_EXSS_SYNCWORD) return QW_STATUS_ERR_UNSUPPORTED;
    if (size < DTS_EXSS_MIN_HEADER_SIZE) return QW_STATUS_ERR_FORMAT;

    for (index = 2u; index < DTS_EXSS_MIN_HEADER_SIZE; ++index)
        header_window = (header_window << 8u) | data[index];
    if (header_window & UINT64_C(0x2000000000))
        encoded_size = (uint32_t)((header_window >> 5u) & UINT64_C(0xfffff));
    else
        encoded_size = (uint32_t)((header_window >> 13u) & UINT64_C(0xffff));

    *frame_size = (size_t)encoded_size + 1u;
    if (*frame_size < DTS_EXSS_MIN_HEADER_SIZE || *frame_size > size) {
        *frame_size = 0u;
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}
