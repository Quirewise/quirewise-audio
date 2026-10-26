#include "audio_wave_ima_format.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    AUDIO_WAVE_IMA_FORMAT_TAG = 0x0011u,
    AUDIO_WAVE_IMA_HEADER_BYTES_PER_CHANNEL = 4u,
    AUDIO_WAVE_IMA_BITS_PER_CODE = 4u
};

int qw_audio_wave_ima_format_parse(const uint8_t *bytes,
                                  size_t size,
                                  QwAudioWaveImaFormat *format,
                                  QwConvertReport *report)
{
    uint16_t format_tag;
    uint16_t block_align;
    uint16_t channels;
    uint16_t sample_bits;
    uint16_t samples_per_block;
    uint16_t extension_size;
    uint32_t expected_samples_per_block;

    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < 2u) return QW_STATUS_ERR_FORMAT;
    format_tag = qw_read_u16_le(bytes);
    if (format_tag != AUDIO_WAVE_IMA_FORMAT_TAG) return QW_STATUS_ERR_UNSUPPORTED;
    if (size < 20u) return QW_STATUS_ERR_FORMAT;

    channels = qw_read_u16_le(bytes + 2u);
    format->sample_rate = qw_read_u32_le(bytes + 4u);
    block_align = qw_read_u16_le(bytes + 12u);
    sample_bits = qw_read_u16_le(bytes + 14u);
    extension_size = qw_read_u16_le(bytes + 16u);
    samples_per_block = qw_read_u16_le(bytes + 18u);

    if (extension_size < 2u || (size_t)extension_size > size - 18u)
        return QW_STATUS_ERR_FORMAT;
    if ((channels != 1u && channels != 2u) ||
        format->sample_rate < 4000u || format->sample_rate > 384000u ||
        sample_bits != AUDIO_WAVE_IMA_BITS_PER_CODE ||
        block_align < (uint16_t)(channels *
                                 AUDIO_WAVE_IMA_HEADER_BYTES_PER_CHANNEL) ||
        (block_align & 3u) != 0u) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native IMA ADPCM supports 4-bit mono or stereo blocks");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    expected_samples_per_block = 1u +
        (((uint32_t)block_align - (uint32_t)channels *
          AUDIO_WAVE_IMA_HEADER_BYTES_PER_CHANNEL) * 2u) / channels;
    if (expected_samples_per_block > UINT16_MAX ||
        samples_per_block != expected_samples_per_block) {
        return QW_STATUS_ERR_FORMAT;
    }

    format->block_align = block_align;
    format->samples_per_block = samples_per_block;
    format->channels = channels;
    format->bits_per_sample = sample_bits;
    format->extension_size = extension_size;
    return QW_STATUS_OK;
}

int qw_audio_wave_ima_validate_blocks(const uint8_t *data,
                                      size_t size,
                                      uint16_t block_align,
                                      uint16_t channels)
{
    size_t block_count;
    size_t block_index;
    size_t header_size;

    if (!data || !size || !block_align || !channels || channels > 2u)
        return QW_STATUS_ERR_FORMAT;
    header_size = (size_t)channels *
                  AUDIO_WAVE_IMA_HEADER_BYTES_PER_CHANNEL;
    if (block_align < header_size || (block_align & 3u) != 0u ||
        size % block_align != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    block_count = size / block_align;
    for (block_index = 0u; block_index < block_count; ++block_index) {
        const uint8_t *block = data + block_index * (size_t)block_align;
        size_t channel;
        for (channel = 0u; channel < channels; ++channel) {
            size_t header_offset = channel *
                                   AUDIO_WAVE_IMA_HEADER_BYTES_PER_CHANNEL;
            if (block[header_offset + 2u] > 88u ||
                block[header_offset + 3u] != 0u) {
                return QW_STATUS_ERR_FORMAT;
            }
        }
    }
    return QW_STATUS_OK;
}
