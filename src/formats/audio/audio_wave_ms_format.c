#include "audio_wave_ms_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    AUDIO_WAVE_MS_FORMAT_TAG = 0x0002u,
    AUDIO_WAVE_MS_BITS_PER_CODE = 4u,
    AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL = 7u,
    AUDIO_WAVE_MS_BASE_EXTENSION_SIZE = 4u,
    AUDIO_WAVE_MS_STANDARD_COEFFICIENTS = 7u,
    AUDIO_WAVE_MS_WAVEFORMATEX_SIZE = 18u,
    AUDIO_WAVE_MS_COEFFICIENT_OFFSET = 22u
};

static const uint16_t audio_wave_ms_standard_coefficients[7][2] = {
    { 256u, 0u }, { 512u, 0xff00u }, { 0u, 0u }, { 192u, 64u },
    { 240u, 0u }, { 460u, 0xff30u }, { 392u, 0xff18u }
};

static int audio_wave_ms_has_standard_coefficients(const uint8_t *format)
{
    size_t index;
    for (index = 0u; index < AUDIO_WAVE_MS_STANDARD_COEFFICIENTS; ++index) {
        size_t offset = AUDIO_WAVE_MS_COEFFICIENT_OFFSET + index * 4u;
        if (qw_read_u16_le(format + offset) !=
                audio_wave_ms_standard_coefficients[index][0] ||
            qw_read_u16_le(format + offset + 2u) !=
                audio_wave_ms_standard_coefficients[index][1]) {
            return 0;
        }
    }
    return 1;
}

int qw_audio_wave_ms_format_parse(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioWaveMsFormat *format,
                                 QwConvertReport *report)
{
    uint16_t format_tag;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t extension_size;
    uint16_t samples_per_block;
    uint16_t coefficient_count;
    uint64_t coded_samples_per_block;
    uint32_t expected_samples_per_block;
    size_t coefficient_bytes;

    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;
    format_tag = qw_read_u16_le(bytes);
    if (format_tag != AUDIO_WAVE_MS_FORMAT_TAG) return QW_STATUS_ERR_UNSUPPORTED;
    if (size < AUDIO_WAVE_MS_WAVEFORMATEX_SIZE +
                   AUDIO_WAVE_MS_BASE_EXTENSION_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }

    channels = qw_read_u16_le(bytes + 2u);
    format->sample_rate = qw_read_u32_le(bytes + 4u);
    block_align = qw_read_u16_le(bytes + 12u);
    bits_per_sample = qw_read_u16_le(bytes + 14u);
    extension_size = qw_read_u16_le(bytes + 16u);
    samples_per_block = qw_read_u16_le(bytes + 18u);
    coefficient_count = qw_read_u16_le(bytes + 20u);

    if (channels == 0u || channels > 6u ||
        format->sample_rate < 4000u || format->sample_rate > 384000u ||
        bits_per_sample != AUDIO_WAVE_MS_BITS_PER_CODE) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native Microsoft ADPCM supports 4-bit mono through 5.1 streams");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (extension_size < AUDIO_WAVE_MS_BASE_EXTENSION_SIZE ||
        (size_t)extension_size > size - AUDIO_WAVE_MS_WAVEFORMATEX_SIZE ||
        coefficient_count == 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    coefficient_bytes = (size_t)coefficient_count * 4u;
    if (coefficient_bytes > (size_t)extension_size -
                            AUDIO_WAVE_MS_BASE_EXTENSION_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (coefficient_count > 256u) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "Microsoft ADPCM predictor indexes address at most 256 coefficient pairs");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (block_align < (uint16_t)(channels *
                                 AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (channels > 2u &&
        ((size_t)block_align - (size_t)channels *
             AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL) % channels != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }

    coded_samples_per_block =
        ((uint64_t)block_align - (uint64_t)channels *
         AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL) * 2u;
    if (coded_samples_per_block % channels != 0u) return QW_STATUS_ERR_FORMAT;
    coded_samples_per_block /= channels;
    if (coded_samples_per_block > UINT16_MAX - 2u) return QW_STATUS_ERR_FORMAT;
    expected_samples_per_block = (uint32_t)coded_samples_per_block + 2u;
    if (!samples_per_block || samples_per_block != expected_samples_per_block)
        return QW_STATUS_ERR_FORMAT;

    format->block_align = block_align;
    format->samples_per_block = samples_per_block;
    format->channels = channels;
    format->bits_per_sample = bits_per_sample;
    format->extension_size = extension_size;
    format->coefficient_count = coefficient_count;
    format->coefficients = bytes + AUDIO_WAVE_MS_COEFFICIENT_OFFSET;
    format->has_standard_coefficients =
        coefficient_count == AUDIO_WAVE_MS_STANDARD_COEFFICIENTS &&
        audio_wave_ms_has_standard_coefficients(bytes);
    return QW_STATUS_OK;
}
