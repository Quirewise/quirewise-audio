#include "audio_caf_channel_layout_parser.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_CHANNEL_LAYOUT_HEADER_SIZE = 12u,
    CAF_CHANNEL_DESCRIPTION_SIZE = 12u,
    CAF_CHANNEL_LAYOUT_USE_BITMAP = 0x00010000u,
    CAF_CHANNEL_BITMAP_KNOWN_BITS = 0x0003ffffu
};

#define CAF_CHANNEL_LAYOUT_TAG(family, channels) \
    ((((uint32_t)(family)) << 16u) | (uint32_t)(channels))

static unsigned caf_channel_layout_popcount(uint32_t value)
{
    unsigned count = 0u;
    while (value) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

static int caf_channel_layout_standard_mask(uint32_t tag, uint32_t *mask)
{
    if (!mask) return 0;
    switch (tag) {
    case CAF_CHANNEL_LAYOUT_TAG(100u, 1u):
        *mask = UINT32_C(0x00000004); /* Center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(101u, 2u):
    case CAF_CHANNEL_LAYOUT_TAG(102u, 2u):
    case CAF_CHANNEL_LAYOUT_TAG(106u, 2u):
        *mask = UINT32_C(0x00000003); /* Left, right */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(108u, 4u):
    case CAF_CHANNEL_LAYOUT_TAG(132u, 4u):
        *mask = UINT32_C(0x00000033); /* Left, right, back left, back right */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(113u, 3u):
        *mask = UINT32_C(0x00000007); /* Left, right, center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(115u, 4u):
        *mask = UINT32_C(0x00000107); /* Left, right, center, back center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(117u, 5u):
        *mask = UINT32_C(0x00000037); /* Left, right, center, surrounds */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(121u, 6u):
        *mask = UINT32_C(0x0000003f); /* 5.1 */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(125u, 7u):
        *mask = UINT32_C(0x0000013f); /* 6.1 */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(126u, 8u):
        *mask = UINT32_C(0x000000ff); /* 7.1 with front left/right center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(131u, 3u):
        *mask = UINT32_C(0x00000103); /* Left, right, back center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(133u, 3u):
        *mask = UINT32_C(0x0000000b); /* Left, right, LFE */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(134u, 4u):
        *mask = UINT32_C(0x0000010b); /* Left, right, LFE, back center */
        return 1;
    case CAF_CHANNEL_LAYOUT_TAG(135u, 5u):
        *mask = UINT32_C(0x0000003b); /* Left, right, LFE, surrounds */
        return 1;
    default:
        return 0;
    }
}

static int caf_channel_description_is_zeroed(const uint8_t *description)
{
    static const uint8_t zero_coordinates[12] = {0};
    return qw_read_u32_be(description + 4u) == 0u &&
           memcmp(description + 8u, zero_coordinates,
                  sizeof(zero_coordinates)) == 0;
}

static void caf_channel_layout_set_unspecified(QwAudioCafChannelLayout *layout)
{
    layout->channel_mask = 0u;
    layout->channel_order_unspecified = 1;
}

int qw_audio_caf_channel_layout_parse(const uint8_t *bytes,
                                      size_t size,
                                      uint16_t channels,
                                      QwAudioCafChannelLayout *layout)
{
    uint32_t tag;
    uint32_t bitmap;
    uint32_t description_count;

    if (!bytes || !layout || !channels) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(layout, 0, sizeof(*layout));
    if (size < CAF_CHANNEL_LAYOUT_HEADER_SIZE) return QW_STATUS_ERR_FORMAT;

    tag = qw_read_u32_be(bytes);
    bitmap = qw_read_u32_be(bytes + 4u);
    description_count = qw_read_u32_be(bytes + 8u);

    if (tag == 0u) {
        size_t descriptions_size;
        size_t expected_size;
        if (bitmap != 0u || description_count != (uint32_t)channels ||
            !qw_checked_size_mul((size_t)description_count,
                                 CAF_CHANNEL_DESCRIPTION_SIZE,
                                 &descriptions_size) ||
            !qw_checked_size_add(CAF_CHANNEL_LAYOUT_HEADER_SIZE,
                                 descriptions_size, &expected_size) ||
            size != expected_size) {
            return QW_STATUS_ERR_FORMAT;
        }

        {
            uint32_t mask = 0u;
            uint32_t previous_bit = 0u;
            int have_previous_bit = 0;
            int representable = 1;
            uint32_t index;

            for (index = 0u; index < description_count; ++index) {
                const uint8_t *description =
                    bytes + CAF_CHANNEL_LAYOUT_HEADER_SIZE +
                    (size_t)index * CAF_CHANNEL_DESCRIPTION_SIZE;
                uint32_t label = qw_read_u32_be(description);
                uint32_t bit;

                if (label < 1u || label > 18u ||
                    !caf_channel_description_is_zeroed(description)) {
                    representable = 0;
                    continue;
                }
                bit = label - 1u;
                if ((mask & (UINT32_C(1) << bit)) ||
                    (have_previous_bit && bit <= previous_bit)) {
                    representable = 0;
                }
                mask |= UINT32_C(1) << bit;
                previous_bit = bit;
                have_previous_bit = 1;
            }
            if (representable &&
                caf_channel_layout_popcount(mask) == (unsigned)channels) {
                layout->channel_mask = mask;
            } else {
                caf_channel_layout_set_unspecified(layout);
            }
        }
        return QW_STATUS_OK;
    }

    if (tag == CAF_CHANNEL_LAYOUT_USE_BITMAP) {
        if (description_count != 0u || size != CAF_CHANNEL_LAYOUT_HEADER_SIZE ||
            caf_channel_layout_popcount(bitmap) != (unsigned)channels) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (bitmap & ~(uint32_t)CAF_CHANNEL_BITMAP_KNOWN_BITS) {
            caf_channel_layout_set_unspecified(layout);
        } else {
            layout->channel_mask = bitmap;
        }
        return QW_STATUS_OK;
    }

    if (bitmap != 0u || description_count != 0u ||
        size != CAF_CHANNEL_LAYOUT_HEADER_SIZE ||
        (tag & UINT32_C(0x0000ffff)) != (uint32_t)channels) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!caf_channel_layout_standard_mask(tag, &layout->channel_mask)) {
        caf_channel_layout_set_unspecified(layout);
    }
    return QW_STATUS_OK;
}

#undef CAF_CHANNEL_LAYOUT_TAG
