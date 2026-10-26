#include "audio_wave_mpeg1_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    WAVE_FORMAT_MPEG = 0x0050u,
    WAVEFORMATEX_SIZE = 18u,
    MPEG1WAVEFORMAT_EXTRA_SIZE = 22u,
    ACM_MPEG_LAYER1 = 0x0001u,
    ACM_MPEG_LAYER2 = 0x0002u,
    ACM_MPEG_LAYER3 = 0x0004u
};

static uint16_t wave_mpeg1_read_u16(const uint8_t *bytes,
                                    int little_endian)
{
    return little_endian ? qw_read_u16_le(bytes) : qw_read_u16_be(bytes);
}

static uint32_t wave_mpeg1_read_u32(const uint8_t *bytes,
                                    int little_endian)
{
    return little_endian ? qw_read_u32_le(bytes) : qw_read_u32_be(bytes);
}

int qw_audio_wave_mpeg1_format_parse(const uint8_t *bytes,
                                     size_t size,
                                     int little_endian,
                                     QwAudioWaveMpeg1Format *format,
                                     QwConvertReport *report)
{
    uint16_t format_tag;
    uint16_t extension_size;
    uint16_t layer_flags;

    (void)report;
    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;
    format_tag = wave_mpeg1_read_u16(bytes, little_endian);
    if (format_tag != WAVE_FORMAT_MPEG) return QW_STATUS_ERR_UNSUPPORTED;
    if (size < WAVEFORMATEX_SIZE) return QW_STATUS_ERR_FORMAT;

    format->channels = wave_mpeg1_read_u16(bytes + 2u, little_endian);
    format->sample_rate = wave_mpeg1_read_u32(bytes + 4u, little_endian);
    format->block_align = wave_mpeg1_read_u16(bytes + 12u, little_endian);
    extension_size = wave_mpeg1_read_u16(bytes + 16u, little_endian);
    if (extension_size < MPEG1WAVEFORMAT_EXTRA_SIZE ||
        (size_t)extension_size > size - WAVEFORMATEX_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }

    if (wave_mpeg1_read_u16(bytes + 14u, little_endian) != 0u ||
        !format->block_align || format->sample_rate > (uint32_t)INT_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!format->channels) return QW_STATUS_ERR_FORMAT;
    if (format->channels > 2u) return QW_STATUS_ERR_UNSUPPORTED;

    layer_flags = wave_mpeg1_read_u16(bytes + WAVEFORMATEX_SIZE,
                                      little_endian);
    switch (layer_flags) {
    case ACM_MPEG_LAYER1: format->layer = 1u; break;
    case ACM_MPEG_LAYER2: format->layer = 2u; break;
    case ACM_MPEG_LAYER3: format->layer = 3u; break;
    default: return QW_STATUS_ERR_UNSUPPORTED;
    }
    format->bitrate = wave_mpeg1_read_u32(bytes + 20u, little_endian);
    return QW_STATUS_OK;
}
