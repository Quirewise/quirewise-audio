#include "audio_aiff_mace_parser.h"

#include "audio_aiff_chunks.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    AIFF_MACE_MAX_CHANNELS = 2u,
    AIFF_MACE3_BYTES_PER_CHANNEL = 2u,
    AIFF_MACE6_BYTES_PER_CHANNEL = 1u,
    AIFF_MACE_SAMPLES_PER_PACKET = 6u
};

int qw_audio_aiff_mace_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffMaceSource *source,
                             QwConvertReport *report)
{
    QwAudioAiffChunks chunks = {0};
    QwAudioAiffMaceCodec codec;
    size_t bytes_per_channel;
    size_t packet_size;
    size_t expected_bytes;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_aiff_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.aifc) return QW_STATUS_ERR_UNSUPPORTED;
    if (memcmp(chunks.compression_type, "MAC3", 4u) == 0) {
        codec = QW_AUDIO_AIFF_MACE3;
        bytes_per_channel = AIFF_MACE3_BYTES_PER_CHANNEL;
    } else if (memcmp(chunks.compression_type, "MAC6", 4u) == 0) {
        codec = QW_AUDIO_AIFF_MACE6;
        bytes_per_channel = AIFF_MACE6_BYTES_PER_CHANNEL;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (chunks.channels == 0u ||
        chunks.channels > AIFF_MACE_MAX_CHANNELS ||
        chunks.sample_rate < 8000u || chunks.sample_rate > 384000u ||
        chunks.sample_bits != 8u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!chunks.frame_count || !chunks.sample_data) return QW_STATUS_ERR_FORMAT;

    packet_size = (size_t)chunks.channels * bytes_per_channel;
    if ((size_t)chunks.frame_count > SIZE_MAX / packet_size)
        return QW_STATUS_ERR_FORMAT;
    expected_bytes = (size_t)chunks.frame_count * packet_size;
    if (chunks.sample_bytes != expected_bytes) return QW_STATUS_ERR_FORMAT;

    source->sample_data = chunks.sample_data;
    source->sample_bytes = chunks.sample_bytes;
    source->packet_size = packet_size;
    source->packet_count = chunks.frame_count;
    source->sample_count = (uint64_t)chunks.frame_count *
                           AIFF_MACE_SAMPLES_PER_PACKET;
    source->sample_rate = chunks.sample_rate;
    source->channels = chunks.channels;
    source->codec = codec;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_aiff_mace_next_frame(const QwAudioAiffMaceSource *source,
                                  size_t packet_index,
                                  QwAudioAiffMaceFrame *frame)
{
    size_t offset;

    if (!source || !frame || !source->sample_data || !source->packet_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet_index >= source->packet_count) return 0;
    offset = packet_index * source->packet_size;
    if (offset > source->sample_bytes ||
        source->packet_size > source->sample_bytes - offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->sample_data + offset;
    frame->size = source->packet_size;
    frame->next_packet_index = packet_index + 1u;
    frame->sample_count = AIFF_MACE_SAMPLES_PER_PACKET;
    return 1;
}
