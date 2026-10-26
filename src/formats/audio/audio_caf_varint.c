#include "audio_caf_varint.h"

#include "quirewise/status.h"

int qw_audio_caf_varint_read(const uint8_t *bytes,
                             size_t size,
                             size_t *position,
                             uint64_t *value)
{
    uint64_t result = 0u;
    unsigned count;
    int terminated = 0;

    if (!bytes || !position || !value || *position > size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    for (count = 0u; count < 10u; ++count) {
        uint8_t byte;
        if (*position >= size || result > (UINT64_MAX >> 7u))
            return QW_STATUS_ERR_FORMAT;
        byte = bytes[(*position)++];
        result = (result << 7u) | (uint64_t)(byte & 0x7fu);
        if ((byte & 0x80u) == 0u) {
            terminated = 1;
            break;
        }
    }
    if (!terminated) return QW_STATUS_ERR_FORMAT;
    *value = result;
    return QW_STATUS_OK;
}
