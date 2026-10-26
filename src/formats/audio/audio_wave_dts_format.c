#include "audio_wave_dts_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum { AUDIO_WAVE_DTS_FORMAT_TAG = 0x0008u };

static uint16_t wave_dts_read_u16(const uint8_t *bytes, int little_endian)
{
    return little_endian ? qw_read_u16_le(bytes) : qw_read_u16_be(bytes);
}

static uint32_t wave_dts_read_u32(const uint8_t *bytes, int little_endian)
{
    return little_endian ? qw_read_u32_le(bytes) : qw_read_u32_be(bytes);
}

int qw_audio_wave_dts_format_parse(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWaveDtsFormat *format,
                                   QwConvertReport *report)
{
    uint16_t format_tag;

    (void)report;
    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;

    format_tag = wave_dts_read_u16(bytes, little_endian);
    if (format_tag != AUDIO_WAVE_DTS_FORMAT_TAG)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (size < 18u) return QW_STATUS_ERR_FORMAT;

    format->channels = wave_dts_read_u16(bytes + 2u, little_endian);
    format->sample_rate = wave_dts_read_u32(bytes + 4u, little_endian);
    format->block_align = wave_dts_read_u16(bytes + 12u, little_endian);
    format->bits_per_sample = wave_dts_read_u16(bytes + 14u, little_endian);
    format->extension_size = wave_dts_read_u16(bytes + 16u,
                                               little_endian);
    if ((size_t)format->extension_size > size - 18u)
        return QW_STATUS_ERR_FORMAT;
    if (!format->channels || format->channels > 64u ||
        !format->sample_rate || format->sample_rate > (uint32_t)INT_MAX) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}
