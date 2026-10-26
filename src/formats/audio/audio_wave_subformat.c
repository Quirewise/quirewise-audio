#include "audio_wave_subformat.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    WAVE_FORMAT_EXTENSIBLE = 0xfffeu,
    WAVE_EXTENSIBLE_SIZE = 22u
};

static uint16_t wave_extensible_guid_tag(const uint8_t *guid)
{
    static const uint8_t suffix[12] = {
        0x00u, 0x00u, 0x10u, 0x00u, 0x80u, 0x00u,
        0x00u, 0xaau, 0x00u, 0x38u, 0x9bu, 0x71u
    };
    uint32_t tag;

    if (!guid || memcmp(guid + 4u, suffix, sizeof(suffix)) != 0) return 0u;
    tag = qw_read_u32_le(guid);
    return tag <= UINT16_MAX ? (uint16_t)tag : 0u;
}

static uint32_t wave_channel_mask_trim_to_channels(uint32_t channel_mask,
                                                   uint16_t channels)
{
    unsigned channel_count = 0u;
    unsigned bit;

    for (bit = 0u; bit < 32u; ++bit) {
        channel_count += (channel_mask >> bit) & 1u;
    }
    for (bit = 32u; channel_count > channels && bit > 0u; --bit) {
        uint32_t flag = UINT32_C(1) << (bit - 1u);
        if (channel_mask & flag) {
            channel_mask &= ~flag;
            --channel_count;
        }
    }
    return channel_mask;
}

int qw_audio_wave_parse_subformat(const uint8_t *bytes,
                                  size_t size,
                                  int little_endian,
                                  QwAudioWaveSubformat *subformat,
                                  QwConvertReport *report)
{
    uint16_t extension_size;
    uint16_t channels;
    uint32_t channel_mask;

    if (!bytes || !subformat) return QW_STATUS_ERR_INVALID_ARGUMENT;
    (void)report;
    memset(subformat, 0, sizeof(*subformat));
    if (size < 16u) return QW_STATUS_ERR_FORMAT;

    subformat->format_tag = little_endian
        ? qw_read_u16_le(bytes) : qw_read_u16_be(bytes);
    subformat->sample_bits = little_endian
        ? qw_read_u16_le(bytes + 14u) : qw_read_u16_be(bytes + 14u);
    subformat->valid_bits = subformat->sample_bits;
    if (subformat->format_tag != WAVE_FORMAT_EXTENSIBLE) return QW_STATUS_OK;

    subformat->extensible = 1;
    if (size < 18u) return QW_STATUS_ERR_FORMAT;
    extension_size = little_endian
        ? qw_read_u16_le(bytes + 16u) : qw_read_u16_be(bytes + 16u);
    if (extension_size < WAVE_EXTENSIBLE_SIZE ||
        (size_t)extension_size > size - 18u) {
        return QW_STATUS_ERR_FORMAT;
    }
    channels = little_endian ? qw_read_u16_le(bytes + 2u)
                             : qw_read_u16_be(bytes + 2u);
    subformat->valid_bits = little_endian ? qw_read_u16_le(bytes + 18u)
                                          : qw_read_u16_be(bytes + 18u);
    channel_mask = little_endian ? qw_read_u32_le(bytes + 20u)
                                 : qw_read_u32_be(bytes + 20u);
    subformat->channel_mask = wave_channel_mask_trim_to_channels(
        channel_mask, channels);
    subformat->format_tag = wave_extensible_guid_tag(bytes + 24u);
    return QW_STATUS_OK;
}
