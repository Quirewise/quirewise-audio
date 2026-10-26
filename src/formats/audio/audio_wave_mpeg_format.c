#include "audio_wave_mpeg_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    WAVE_FORMAT_MPEGLAYER3 = 0x0055u,
    WAVE_MPEG_EXTENSION_BYTES = 12u,
    WAVE_MPEG_ID_MPEG = 1u,
    WAVE_MPEG_PADDING_ISO = 0u,
    WAVE_MPEG_PADDING_ON = 1u,
    WAVE_MPEG_PADDING_OFF = 2u
};

static uint16_t wave_mpeg_read_u16(const uint8_t *bytes, int little_endian)
{
    return little_endian ? qw_read_u16_le(bytes) : qw_read_u16_be(bytes);
}

static uint32_t wave_mpeg_read_u32(const uint8_t *bytes, int little_endian)
{
    return little_endian ? qw_read_u32_le(bytes) : qw_read_u32_be(bytes);
}

int qw_audio_wave_mpeg_format_parse(const uint8_t *bytes,
                                    size_t size,
                                    int little_endian,
                                    QwAudioWaveMpegFormat *format,
                                    QwConvertReport *report)
{
    uint16_t format_tag;
    uint16_t extension_size;
    uint16_t id;

    (void)report;
    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;
    format_tag = wave_mpeg_read_u16(bytes, little_endian);
    if (format_tag != WAVE_FORMAT_MPEGLAYER3) return QW_STATUS_ERR_UNSUPPORTED;
    if (size < 18u) return QW_STATUS_ERR_FORMAT;

    format->channels = wave_mpeg_read_u16(bytes + 2u, little_endian);
    format->sample_rate = wave_mpeg_read_u32(bytes + 4u, little_endian);
    format->average_bytes_per_second =
        wave_mpeg_read_u32(bytes + 8u, little_endian);
    format->block_align = wave_mpeg_read_u16(bytes + 12u, little_endian);
    format->bits_per_sample = wave_mpeg_read_u16(bytes + 14u, little_endian);
    extension_size = wave_mpeg_read_u16(bytes + 16u, little_endian);
    if (extension_size < WAVE_MPEG_EXTENSION_BYTES ||
        (size_t)extension_size > size - 18u) {
        return QW_STATUS_ERR_FORMAT;
    }

    id = wave_mpeg_read_u16(bytes + 18u, little_endian);
    if (id != WAVE_MPEG_ID_MPEG) return QW_STATUS_ERR_UNSUPPORTED;
    format->flags = wave_mpeg_read_u32(bytes + 20u, little_endian);
    format->block_size = wave_mpeg_read_u16(bytes + 24u, little_endian);
    format->frames_per_block = wave_mpeg_read_u16(bytes + 26u, little_endian);
    format->codec_delay_samples = wave_mpeg_read_u16(bytes + 28u,
                                                     little_endian);
    if (format->flags != WAVE_MPEG_PADDING_ISO &&
        format->flags != WAVE_MPEG_PADDING_ON &&
        format->flags != WAVE_MPEG_PADDING_OFF) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!format->sample_rate || format->sample_rate > (uint32_t)INT_MAX ||
        !format->block_align || !format->block_size ||
        !format->frames_per_block) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!format->channels || format->channels > 2u)
        return QW_STATUS_ERR_UNSUPPORTED;
    return QW_STATUS_OK;
}
