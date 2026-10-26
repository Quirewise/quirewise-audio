#include "audio_au_parser.h"

#include "../../core/byte_utils.h"
#include "audio_au_adpcm_parser.h"
#include "audio_pcm_shape.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    AU_HEADER_SIZE = 24u
};

#define AU_UNKNOWN_DATA_SIZE UINT32_MAX

static uint16_t audio_au_sample_bits(uint32_t encoding)
{
    switch (encoding) {
    case QW_AUDIO_AU_ENCODING_ULAW:
    case QW_AUDIO_AU_ENCODING_LINEAR_8:
    case QW_AUDIO_AU_ENCODING_ALAW:
        return 8u;
    case QW_AUDIO_AU_ENCODING_LINEAR_16:
        return 16u;
    case QW_AUDIO_AU_ENCODING_LINEAR_24:
        return 24u;
    case QW_AUDIO_AU_ENCODING_LINEAR_32:
    case QW_AUDIO_AU_ENCODING_FLOAT:
        return 32u;
    case QW_AUDIO_AU_ENCODING_DOUBLE:
        return 64u;
    default:
        return 0u;
    }
}

int qw_audio_au_parse(const uint8_t *bytes,
                      size_t size,
                      QwAudioAuSource *source,
                      QwConvertReport *report)
{
    uint32_t data_offset;
    uint32_t declared_data_size;
    uint16_t sample_bits;
    uint64_t frame_bytes;
    size_t available_bytes;
    size_t sample_bytes;
    QwAudioAuAdpcmInfo adpcm_info;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < AU_HEADER_SIZE || memcmp(bytes, ".snd", 4u) != 0)
        return QW_STATUS_ERR_FORMAT;

    data_offset = qw_read_u32_be(bytes + 4u);
    declared_data_size = qw_read_u32_be(bytes + 8u);
    source->encoding = qw_read_u32_be(bytes + 12u);
    source->sample_rate = qw_read_u32_be(bytes + 16u);
    {
        uint32_t channels = qw_read_u32_be(bytes + 20u);
        if (channels < 1u || channels > QW_AUDIO_PCM_MAX_CHANNELS) {
            if (report) {
                (void)snprintf(report->note, sizeof(report->note),
                               "native AU parser supports 1-64 channels");
            }
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        source->channels = (uint16_t)channels;
    }
    if (data_offset < AU_HEADER_SIZE || (uint64_t)data_offset > size)
        return QW_STATUS_ERR_FORMAT;

    if (!qw_audio_pcm_rate_channels_supported(source->channels,
                                               source->sample_rate)) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native AU parser supports positive int-range sample rates");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    available_bytes = size - (size_t)data_offset;
    sample_bytes = declared_data_size == AU_UNKNOWN_DATA_SIZE
        ? available_bytes : (size_t)declared_data_size;
    if (sample_bytes > available_bytes) return QW_STATUS_ERR_FORMAT;

    sample_bits = audio_au_sample_bits(source->encoding);
    if (sample_bits) {
        frame_bytes = (uint64_t)source->channels * (sample_bits / 8u);
        if (!frame_bytes || sample_bytes % frame_bytes != 0u)
            return QW_STATUS_ERR_FORMAT;
        source->frames = sample_bytes / frame_bytes;
        if (!source->frames) return QW_STATUS_ERR_FORMAT;
    } else if (qw_audio_au_adpcm_describe(source->encoding,
                                           &adpcm_info)) {
        rc = qw_audio_au_adpcm_sample_count(
            source->encoding, sample_bytes, source->channels,
            &source->frames);
        if (rc != QW_STATUS_OK) return rc;
    }

    source->samples = bytes + data_offset;
    source->sample_bytes = sample_bytes;
    source->has_metadata = data_offset > AU_HEADER_SIZE ||
                           sample_bytes < available_bytes;
    return QW_STATUS_OK;
}

int qw_audio_au_parse_pcm(const uint8_t *bytes,
                          size_t size,
                          QwAudioPcmSource *source,
                          QwConvertReport *report)
{
    QwAudioAuSource au = {0};
    uint16_t sample_bits;
    int floating_point;
    int rc;

    if (!source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_au_parse(bytes, size, &au, report);
    if (rc != QW_STATUS_OK) return rc;
    switch (au.encoding) {
    case QW_AUDIO_AU_ENCODING_LINEAR_8:
    case QW_AUDIO_AU_ENCODING_LINEAR_16:
    case QW_AUDIO_AU_ENCODING_LINEAR_24:
    case QW_AUDIO_AU_ENCODING_LINEAR_32:
        floating_point = 0;
        break;
    case QW_AUDIO_AU_ENCODING_FLOAT:
    case QW_AUDIO_AU_ENCODING_DOUBLE:
        floating_point = 1;
        break;
    default:
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native AU PCM adapter accepts linear PCM and IEEE float encodings");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    sample_bits = audio_au_sample_bits(au.encoding);
    source->samples = au.samples;
    source->sample_bytes = au.sample_bytes;
    source->sample_rate = au.sample_rate;
    source->frames = au.frames;
    source->channels = au.channels;
    source->sample_bits = sample_bits;
    source->little_endian = 0;
    source->floating_point = floating_point;
    source->unsigned_8bit = 0;
    source->has_metadata = au.has_metadata;
    return QW_STATUS_OK;
}
