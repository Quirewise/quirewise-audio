#include "audio_wave_gsm_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    AUDIO_WAVE_GSM_FORMAT_TAG = 0x0031u,
    AUDIO_WAVE_GSM_DESCRIPTOR_SIZE = 20u,
    AUDIO_WAVE_GSM_BLOCK_ALIGN = 65u,
    AUDIO_WAVE_GSM_SAMPLES_PER_BLOCK = 320u
};

int qw_audio_wave_gsm_format_parse(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWaveGsmFormat *format,
                                   QwConvertReport *report)
{
    uint16_t format_tag;
    uint64_t expected_average_bytes;

    (void)report;
    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;
    if (!little_endian) return QW_STATUS_ERR_UNSUPPORTED;

    format_tag = qw_read_u16_le(bytes);
    if (format_tag != AUDIO_WAVE_GSM_FORMAT_TAG)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (size < AUDIO_WAVE_GSM_DESCRIPTOR_SIZE) return QW_STATUS_ERR_FORMAT;

    format->channels = qw_read_u16_le(bytes + 2u);
    format->sample_rate = qw_read_u32_le(bytes + 4u);
    format->average_bytes_per_second = qw_read_u32_le(bytes + 8u);
    format->block_align = qw_read_u16_le(bytes + 12u);
    format->bits_per_sample = qw_read_u16_le(bytes + 14u);
    format->extension_size = qw_read_u16_le(bytes + 16u);
    format->samples_per_block = qw_read_u16_le(bytes + 18u);

    if (format->extension_size < 2u ||
        (size_t)format->extension_size > size - 18u) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (format->channels != 1u || format->sample_rate == 0u ||
        format->sample_rate > (uint32_t)INT_MAX ||
        format->block_align != AUDIO_WAVE_GSM_BLOCK_ALIGN ||
        format->bits_per_sample != 0u ||
        format->samples_per_block != AUDIO_WAVE_GSM_SAMPLES_PER_BLOCK) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    expected_average_bytes =
        ((uint64_t)format->sample_rate * AUDIO_WAVE_GSM_BLOCK_ALIGN) /
        AUDIO_WAVE_GSM_SAMPLES_PER_BLOCK;
    if (expected_average_bytes > UINT32_MAX ||
        format->average_bytes_per_second != expected_average_bytes) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}
