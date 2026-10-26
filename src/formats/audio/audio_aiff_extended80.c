#include "audio_aiff_extended80.h"

#include "../../core/byte_utils.h"

#include <limits.h>

int qw_audio_aiff_extended80_to_rate(const uint8_t bytes[10],
                                    uint32_t *rate_out)
{
    uint16_t exponent;
    uint64_t significand;
    int shift;
    uint64_t rate;

    if (!bytes || !rate_out) return 0;
    exponent = qw_read_u16_be(bytes);
    significand = ((uint64_t)qw_read_u32_be(bytes + 2u) << 32) |
                  qw_read_u32_be(bytes + 6u);
    if ((exponent & 0x8000u) || (exponent & 0x7fffu) == 0x7fffu ||
        significand == 0u || !(significand & (UINT64_C(1) << 63))) {
        return 0;
    }

    shift = 16446 - (int)(exponent & 0x7fffu);
    if (shift < 0 || shift > 63) return 0;
    if (!shift) {
        rate = significand;
    } else {
        uint64_t halfway = UINT64_C(1) << (shift - 1);
        uint64_t remainder = significand & ((UINT64_C(1) << shift) - 1u);

        rate = significand >> shift;
        if (remainder >= halfway) ++rate;
    }
    if (!rate || rate > UINT32_MAX || rate > (uint64_t)INT_MAX) return 0;
    *rate_out = (uint32_t)rate;
    return 1;
}
