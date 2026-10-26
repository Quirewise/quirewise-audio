#include "audio_aiff_ima4_parser.h"

#include "audio_aiff_chunks.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    AIFF_IMA4_BYTES_PER_CHANNEL = 34u,
    AIFF_IMA4_SAMPLES_PER_PACKET = 64u,
    AIFF_IMA4_MAX_CHANNELS = 2u
};

int qw_audio_aiff_ima4_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffIma4Source *source,
                             QwConvertReport *report)
{
    QwAudioAiffChunks chunks = {0};
    size_t packet_size;
    size_t expected_bytes;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_aiff_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.aifc || memcmp(chunks.compression_type, "ima4", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (chunks.channels == 0u ||
        chunks.channels > AIFF_IMA4_MAX_CHANNELS ||
        chunks.sample_rate < 8000u || chunks.sample_rate > 384000u ||
        chunks.sample_bits != 16u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!chunks.frame_count || !chunks.sample_data) return QW_STATUS_ERR_FORMAT;

    packet_size = (size_t)chunks.channels * AIFF_IMA4_BYTES_PER_CHANNEL;
    if ((size_t)chunks.frame_count > SIZE_MAX / packet_size)
        return QW_STATUS_ERR_FORMAT;
    expected_bytes = (size_t)chunks.frame_count * packet_size;
    if (chunks.sample_bytes != expected_bytes) return QW_STATUS_ERR_FORMAT;

    source->sample_data = chunks.sample_data;
    source->sample_bytes = chunks.sample_bytes;
    source->packet_size = packet_size;
    source->packet_count = chunks.frame_count;
    source->sample_count = (uint64_t)chunks.frame_count *
                           AIFF_IMA4_SAMPLES_PER_PACKET;
    source->sample_rate = chunks.sample_rate;
    source->channels = chunks.channels;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_aiff_ima4_next_frame(const QwAudioAiffIma4Source *source,
                                  size_t packet_index,
                                  QwAudioAiffIma4Frame *frame)
{
    size_t byte_offset;
    if (!source || !frame || !source->sample_data || !source->packet_size ||
        !source->packet_count || !source->channels) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (packet_index >= source->packet_count) return 0;
    if (packet_index > SIZE_MAX / source->packet_size)
        return QW_STATUS_ERR_FORMAT;
    byte_offset = packet_index * source->packet_size;
    if (byte_offset > source->sample_bytes ||
        source->packet_size > source->sample_bytes - byte_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->sample_data + byte_offset;
    frame->size = source->packet_size;
    frame->next_packet_index = packet_index + 1u;
    frame->sample_count = AIFF_IMA4_SAMPLES_PER_PACKET;
    return 1;
}
