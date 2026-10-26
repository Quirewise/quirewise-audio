#include "audio_wave_format.h"

#include "../../core/byte_utils.h"
#include "audio_pcm_shape.h"
#include "audio_wave_subformat.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    WAVE_FORMAT_PCM = 0x0001u,
    WAVE_FORMAT_IEEE_FLOAT = 0x0003u
};

static unsigned wave_channel_mask_count(uint32_t channel_mask)
{
    unsigned channel_count = 0u;
    while (channel_mask) {
        channel_count += channel_mask & 1u;
        channel_mask >>= 1u;
    }
    return channel_count;
}

int qw_audio_wave_parse_pcm_format(const uint8_t *bytes,
                                   size_t size,
                                   int little_endian,
                                   QwAudioWavePcmFormat *format,
                                   QwConvertReport *report)
{
    QwAudioWaveSubformat subformat;
    uint16_t format_tag;
    uint16_t valid_bits;
    size_t bytes_per_sample;
    uint64_t expected_rate;
    int rc;

    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    rc = qw_audio_wave_parse_subformat(bytes, size, little_endian,
                                       &subformat, report);
    if (rc != QW_STATUS_OK) return rc;
    format_tag = subformat.format_tag;
    format->channels = little_endian
        ? qw_read_u16_le(bytes + 2u) : qw_read_u16_be(bytes + 2u);
    format->sample_rate = little_endian
        ? qw_read_u32_le(bytes + 4u) : qw_read_u32_be(bytes + 4u);
    format->byte_rate = little_endian
        ? qw_read_u32_le(bytes + 8u) : qw_read_u32_be(bytes + 8u);
    format->block_align = little_endian
        ? qw_read_u16_le(bytes + 12u) : qw_read_u16_be(bytes + 12u);
    format->sample_bits = subformat.sample_bits;
    format->channel_mask = subformat.channel_mask;
    format->channel_layout_unspecified = subformat.extensible &&
        wave_channel_mask_count(format->channel_mask) < format->channels;
    valid_bits = subformat.valid_bits;

    if (format_tag == WAVE_FORMAT_IEEE_FLOAT) {
        format->floating_point = 1;
    } else if (format_tag != WAVE_FORMAT_PCM) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native WAVE parser accepts integer or IEEE float PCM only");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (valid_bits == 0u || valid_bits > format->sample_bits) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "WAVE valid bits must fit the sample container");
        }
        return QW_STATUS_ERR_FORMAT;
    }
    if (!qw_audio_pcm_shape_supported(format->channels,
                                     format->sample_rate,
                                     format->sample_bits,
                                     format->floating_point)) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native PCM parser supports 1-64 channels, positive int-range sample rates, integer 8/16/24/32-bit or float 32/64-bit samples");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    bytes_per_sample = (size_t)format->sample_bits / 8u;
    if (!bytes_per_sample ||
        format->channels > UINT16_MAX / bytes_per_sample) {
        return QW_STATUS_ERR_FORMAT;
    }
    {
        uint16_t expected_align =
            (uint16_t)((size_t)format->channels * bytes_per_sample);
        expected_rate = (uint64_t)format->sample_rate * expected_align;
        if (format->block_align != expected_align ||
            expected_rate > UINT32_MAX ||
            format->byte_rate != (uint32_t)expected_rate) {
            return QW_STATUS_ERR_FORMAT;
        }
    }
    format->unsigned_8bit = !format->floating_point &&
                            format->sample_bits == 8u;
    return QW_STATUS_OK;
}
