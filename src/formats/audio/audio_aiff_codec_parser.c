#include "audio_aiff_codec_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    AIFF_CODEC_PACKET_TARGET = 4096u,
    AIFF_CODEC_MAX_EXTRADATA_SIZE = 1024u * 1024u,
    AIFF_QCELP_WAVE_RATE_OFFSET = 24u,
    AIFF_QDM2_WAVE_BLOCK_DURATION_OFFSET = 9u * 4u,
    AIFF_QDM2_WAVE_BLOCK_ALIGN_OFFSET = 11u * 4u,
    AIFF_QDM2_WAVE_MIN_SIZE = 12u * 4u
};

static uint64_t aiff_codec_sample_count(size_t blocks,
                                        uint32_t samples_per_block)
{
    if (samples_per_block &&
        (uint64_t)blocks > UINT64_MAX / samples_per_block) {
        return 0u;
    }
    return (uint64_t)blocks * samples_per_block;
}

static int aiff_codec_assign(const QwAudioAiffChunks *chunks,
                             QwAudioAiffCodecSource *source)
{
    const uint8_t *tag = chunks->compression_type;

    if (chunks->has_apcm) {
        source->codec = QW_AUDIO_AIFF_CODEC_ADPCM_XA;
        source->block_align = chunks->apcm_block_align;
        source->coded_bits = 4u;
        source->sample_bits = 16u;
        source->sample_count = chunks->frame_count;
    } else if (memcmp(tag, "GSM ", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_GSM;
        source->block_align = 33u;
        source->block_duration = 160u;
        source->one_block_per_packet = 1;
    } else if (memcmp(tag, "G722", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_G722;
        source->block_align = 1u;
    } else if (memcmp(tag, "G726", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_G726;
        source->block_align = 1u;
        source->coded_bits = 5u;
    } else if (memcmp(tag, "G728", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_G728;
        source->block_align = 5u;
        source->block_duration = 20u;
    } else if (memcmp(tag, "QDMC", 4u) == 0 ||
               memcmp(tag, "QDM2", 4u) == 0) {
        if (!chunks->has_codec_extradata ||
            chunks->codec_extradata_size < AIFF_QDM2_WAVE_MIN_SIZE) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        source->codec = memcmp(tag, "QDMC", 4u) == 0
            ? QW_AUDIO_AIFF_CODEC_QDMC : QW_AUDIO_AIFF_CODEC_QDM2;
        source->block_duration = qw_read_u32_be(
            chunks->codec_extradata + AIFF_QDM2_WAVE_BLOCK_DURATION_OFFSET);
        source->block_align = qw_read_u32_be(
            chunks->codec_extradata + AIFF_QDM2_WAVE_BLOCK_ALIGN_OFFSET);
        source->extradata = chunks->codec_extradata;
        source->extradata_size = chunks->codec_extradata_size;
        source->one_block_per_packet =
            source->codec == QW_AUDIO_AIFF_CODEC_QDM2;
        if (!source->block_duration || !source->block_align ||
            source->block_align > (size_t)INT_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
    } else if (memcmp(tag, "Qclp", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_QCELP;
        source->block_align = 35u;
        source->block_duration = 160u;
        source->one_block_per_packet = 1;
        if (chunks->has_codec_extradata) {
            source->extradata = chunks->codec_extradata;
            source->extradata_size = chunks->codec_extradata_size;
        }
        if (chunks->has_codec_extradata &&
            chunks->codec_extradata_size > AIFF_QCELP_WAVE_RATE_OFFSET &&
            chunks->codec_extradata[AIFF_QCELP_WAVE_RATE_OFFSET] == 'H') {
            source->block_align = 17u;
        }
    } else if (memcmp(tag, "CBD2", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_CBD2;
        source->block_align = chunks->channels;
    } else if (memcmp(tag, "SDX2", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_SDX2;
        source->block_align = chunks->channels;
    } else if (memcmp(tag, "ADP4", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_ADP4;
        source->block_align = chunks->channels;
        source->coded_bits = 4u;
        if (chunks->has_codec_extradata) {
            source->extradata = chunks->codec_extradata;
            source->extradata_size = chunks->codec_extradata_size;
        }
    } else if (memcmp(tag, "VAPC", 4u) == 0) {
        source->codec = QW_AUDIO_AIFF_CODEC_N64;
        source->block_align = 9u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (!source->block_align || source->sample_bytes % source->block_align)
        return QW_STATUS_ERR_FORMAT;
    if (source->block_duration) {
        source->sample_count = aiff_codec_sample_count(
            chunks->frame_count, source->block_duration);
        if (chunks->frame_count && !source->sample_count)
            return QW_STATUS_ERR_RESOURCE_LIMIT;
    }
    return QW_STATUS_OK;
}

int qw_audio_aiff_codec_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioAiffCodecSource *source,
                              QwConvertReport *report)
{
    QwAudioAiffChunks chunks = {0};
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_aiff_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.aifc && !chunks.has_apcm) return QW_STATUS_ERR_UNSUPPORTED;
    if (chunks.has_codec_extradata &&
        chunks.codec_extradata_size > AIFF_CODEC_MAX_EXTRADATA_SIZE) {
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    }
    if (!chunks.frame_count || !chunks.sample_data || !chunks.sample_bytes)
        return QW_STATUS_ERR_FORMAT;
    if (!chunks.channels || chunks.channels > 8u ||
        chunks.sample_rate < 8000u || chunks.sample_rate > 384000u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    source->sample_data = chunks.sample_data;
    source->sample_bytes = chunks.sample_bytes;
    source->sample_rate = chunks.sample_rate;
    source->channels = chunks.channels;
    source->sample_bits = chunks.sample_bits;
    source->has_metadata = chunks.has_metadata;
    rc = aiff_codec_assign(&chunks, source);
    if (rc != QW_STATUS_OK) return rc;

    if ((source->codec == QW_AUDIO_AIFF_CODEC_GSM ||
         source->codec == QW_AUDIO_AIFF_CODEC_QCELP ||
         source->codec == QW_AUDIO_AIFF_CODEC_G728) &&
        (source->channels != 1u || source->sample_bits != 16u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->codec == QW_AUDIO_AIFF_CODEC_GSM &&
        source->sample_rate != 8000u) return QW_STATUS_ERR_UNSUPPORTED;
    if (source->codec == QW_AUDIO_AIFF_CODEC_G722 &&
        (source->sample_rate != 16000u || source->channels != 1u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if ((source->codec == QW_AUDIO_AIFF_CODEC_G726 ||
         source->codec == QW_AUDIO_AIFF_CODEC_G728 ||
         source->codec == QW_AUDIO_AIFF_CODEC_QCELP) &&
        source->sample_rate != 8000u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if ((source->codec == QW_AUDIO_AIFF_CODEC_QDMC ||
         source->codec == QW_AUDIO_AIFF_CODEC_QDM2) &&
        source->channels > 2u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->codec == QW_AUDIO_AIFF_CODEC_N64 &&
        (source->channels != 1u || source->sample_bits != 16u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->codec == QW_AUDIO_AIFF_CODEC_ADPCM_XA &&
        source->channels > 2u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

int qw_audio_aiff_codec_next_packet(const QwAudioAiffCodecSource *source,
                                    size_t position,
                                    QwAudioAiffCodecPacket *packet)
{
    size_t remaining;
    size_t packet_size;
    size_t target_size;

    if (!source || !packet || !source->sample_data || !source->sample_bytes ||
        !source->block_align) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (position >= source->sample_bytes) return 0;
    remaining = source->sample_bytes - position;
    if (source->one_block_per_packet) {
        packet_size = source->block_align;
    } else {
        target_size = source->block_align > AIFF_CODEC_PACKET_TARGET
            ? source->block_align : AIFF_CODEC_PACKET_TARGET;
        packet_size = (target_size / source->block_align) *
                      source->block_align;
        if (packet_size > remaining) packet_size = remaining;
    }
    if (!packet_size || packet_size > remaining) return QW_STATUS_ERR_FORMAT;
    packet->data = source->sample_data + position;
    packet->size = packet_size;
    packet->next_position = position + packet_size;
    return 1;
}
