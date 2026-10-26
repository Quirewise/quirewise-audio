#include "audio_caf_mace_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_MACE_DESCRIPTION_SIZE = 32u,
    CAF_MACE_FRAMES_PER_PACKET = 6u,
    CAF_MACE3_BYTES_PER_CHANNEL = 2u,
    CAF_MACE6_BYTES_PER_CHANNEL = 1u
};

static int caf_mace_parse_description(const uint8_t *bytes,
                                      size_t size,
                                      QwAudioCafMaceSource *source)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint32_t channels;
    uint32_t bytes_per_channel;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_MACE_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "MAC3", 4u) == 0) {
        source->codec = QW_AUDIO_CAF_MACE3;
        bytes_per_channel = CAF_MACE3_BYTES_PER_CHANNEL;
    } else if (memcmp(bytes + 8u, "MAC6", 4u) == 0) {
        source->codec = QW_AUDIO_CAF_MACE6;
        bytes_per_channel = CAF_MACE6_BYTES_PER_CHANNEL;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    if (!(sample_rate >= 8000.0 && sample_rate <= 384000.0) ||
        (double)(uint32_t)sample_rate != sample_rate ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        frames_per_packet != CAF_MACE_FRAMES_PER_PACKET ||
        channels == 0u || channels > 2u ||
        qw_read_u32_be(bytes + 28u) != 0u ||
        bytes_per_packet != channels * bytes_per_channel) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->sample_rate = (uint32_t)sample_rate;
    source->channels = (uint16_t)channels;
    source->packet_size = bytes_per_packet;
    return QW_STATUS_OK;
}

int qw_audio_caf_mace_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafMaceSource *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t packet_count;
    uint64_t total_frames;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_mace_parse_description(chunks.description,
                                    chunks.description_size, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size || !source->packet_size ||
        chunks.audio_data_size % source->packet_size != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_count = chunks.audio_data_size / source->packet_size;
    if (!packet_count || packet_count > UINT64_MAX / CAF_MACE_FRAMES_PER_PACKET)
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    total_frames = (uint64_t)packet_count * CAF_MACE_FRAMES_PER_PACKET;

    if (chunks.packet_table) {
        if (chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_table.packet_count != 0u) return QW_STATUS_ERR_FORMAT;
        rc = qw_audio_caf_packet_table_validate_frames(
            &packet_table, packet_count, CAF_MACE_FRAMES_PER_PACKET);
        if (rc != QW_STATUS_OK) return rc;
        while (table_position < chunks.packet_table_size) {
            if (chunks.packet_table[table_position++] != 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
    } else {
        packet_table.valid_frame_count = total_frames;
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_mace_next_frame(const QwAudioCafMaceSource *source,
                                 size_t packet_index,
                                 QwAudioCafMaceFrame *frame)
{
    size_t offset;

    if (!source || !frame || !source->data || !source->packet_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet_index >= source->packet_count) return 0;
    if (packet_index > SIZE_MAX / source->packet_size)
        return QW_STATUS_ERR_FORMAT;
    offset = packet_index * source->packet_size;
    if (offset > source->data_size ||
        source->packet_size > source->data_size - offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->data + offset;
    frame->size = source->packet_size;
    frame->next_packet_index = packet_index + 1u;
    frame->sample_count = CAF_MACE_FRAMES_PER_PACKET;
    return 1;
}
