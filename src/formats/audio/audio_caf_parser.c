#include "audio_caf_parser.h"

#include "audio_caf_channel_layout_parser.h"
#include "audio_caf_chunks.h"
#include "audio_pcm_shape.h"
#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    CAF_DESCRIPTION_SIZE = 32u,
    CAF_FORMAT_FLAG_FLOAT = 1u,
    CAF_FORMAT_FLAG_LITTLE_ENDIAN = 2u
};

typedef struct QwAudioCafPcmDescription {
    uint32_t sample_rate;
    uint32_t bytes_per_frame;
    uint16_t channels;
    uint16_t sample_bits;
    uint8_t container_bytes_per_sample;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    int floating_point;
    int little_endian;
    int aligned_high;
} QwAudioCafPcmDescription;

static int caf_parse_pcm_description(const uint8_t *bytes,
                                     size_t size,
                                     QwAudioCafPcmDescription *description,
                                     QwConvertReport *report)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t flags;
    uint32_t channels;
    uint32_t sample_bits;
    uint64_t bytes_per_frame;
    uint64_t expected_bytes_per_packet;
    uint32_t container_bytes_per_sample;
    uint32_t container_bits;

    if (!bytes || !description) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "lpcm", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;

    memset(description, 0, sizeof(*description));
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    flags = qw_read_u32_be(bytes + 12u);
    description->bytes_per_packet = qw_read_u32_be(bytes + 16u);
    description->frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    sample_bits = qw_read_u32_be(bytes + 28u);

    if (!(sample_rate >= 1.0 && sample_rate <= (double)INT_MAX) ||
        (flags & ~(uint32_t)(CAF_FORMAT_FLAG_FLOAT |
                             CAF_FORMAT_FLAG_LITTLE_ENDIAN)) != 0u ||
        channels < 1u || channels > QW_AUDIO_PCM_MAX_CHANNELS ||
        sample_bits > UINT16_MAX) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native CAF parser supports LPCM with 1-64 channels and an integral sample rate");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->sample_rate = (uint32_t)sample_rate;
    if ((double)description->sample_rate != sample_rate) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native CAF parser requires an integral sample rate");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->channels = (uint16_t)channels;
    description->sample_bits = (uint16_t)sample_bits;
    description->floating_point =
        (flags & CAF_FORMAT_FLAG_FLOAT) != 0u;
    description->little_endian =
        (flags & CAF_FORMAT_FLAG_LITTLE_ENDIAN) != 0u;

    if (description->floating_point) {
        if (sample_bits != 32u && sample_bits != 64u)
            return QW_STATUS_ERR_UNSUPPORTED;
    } else if (!sample_bits || sample_bits > 32u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!description->frames_per_packet) return QW_STATUS_ERR_FORMAT;
    if (description->bytes_per_packet % description->frames_per_packet != 0u)
        return QW_STATUS_ERR_FORMAT;
    bytes_per_frame = description->bytes_per_packet /
                      description->frames_per_packet;
    if (bytes_per_frame % channels != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    container_bytes_per_sample = (uint32_t)(bytes_per_frame / channels);
    if (!container_bytes_per_sample || container_bytes_per_sample > 8u)
        return QW_STATUS_ERR_UNSUPPORTED;
    container_bits = container_bytes_per_sample * 8u;
    if (description->floating_point) {
        if (container_bits != sample_bits) return QW_STATUS_ERR_UNSUPPORTED;
    } else if (container_bytes_per_sample > 4u ||
               sample_bits > container_bits) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->container_bytes_per_sample =
        (uint8_t)container_bytes_per_sample;
    description->aligned_high =
        !description->floating_point && container_bits > sample_bits;
    expected_bytes_per_packet =
        bytes_per_frame * (uint64_t)description->frames_per_packet;
    if (!bytes_per_frame || bytes_per_frame > UINT32_MAX ||
        expected_bytes_per_packet > UINT32_MAX ||
        description->bytes_per_packet != (uint32_t)expected_bytes_per_packet) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->bytes_per_frame = (uint32_t)bytes_per_frame;
    return QW_STATUS_OK;
}

int qw_audio_caf_parse_pcm(const uint8_t *bytes,
                           size_t size,
                           QwAudioPcmSource *source,
                           QwConvertReport *report)
{
    QwAudioCafChunks chunks;
    QwAudioCafPcmDescription description = {0};
    QwAudioCafChannelLayout channel_layout = {0};
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_parse_pcm_description(chunks.description,
                                   chunks.description_size,
                                   &description, report);
    if (rc != QW_STATUS_OK) return rc;
    if (chunks.channel_layout) {
        rc = qw_audio_caf_channel_layout_parse(chunks.channel_layout,
                                               chunks.channel_layout_size,
                                               description.channels,
                                               &channel_layout);
        if (rc != QW_STATUS_OK) return rc;
    } else if (description.channels > 2u) {
        channel_layout.channel_order_unspecified = 1;
    }
    if (!chunks.audio_data || !description.bytes_per_frame ||
        chunks.audio_data_size % description.bytes_per_packet != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }

    source->samples = chunks.audio_data;
    source->sample_bytes = chunks.audio_data_size;
    source->sample_rate = description.sample_rate;
    source->frames = chunks.audio_data_size / description.bytes_per_frame;
    source->channel_mask = channel_layout.channel_mask;
    source->channel_layout_unspecified =
        channel_layout.channel_order_unspecified;
    source->channels = description.channels;
    source->sample_bits = description.sample_bits;
    source->container_bytes_per_sample =
        description.container_bytes_per_sample;
    source->aligned_high = description.aligned_high;
    source->little_endian = description.little_endian;
    source->floating_point = description.floating_point;
    source->unsigned_8bit = 0;
    source->has_metadata = chunks.has_metadata;
    if (!source->frames) return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
