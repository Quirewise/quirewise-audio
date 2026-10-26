#include "audio_caf_ima4_parser.h"

#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_DESCRIPTION_SIZE = 32u,
    CAF_IMA4_BYTES_PER_CHANNEL = 34u,
    CAF_IMA4_FRAMES_PER_PACKET = 64u
};

static int caf_ima4_parse_description(const uint8_t *bytes,
                                      size_t size,
                                      uint32_t *sample_rate,
                                      uint16_t *channels,
                                      size_t *packet_size)
{
    uint64_t sample_rate_bits;
    double rate;
    uint32_t flags;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint32_t channel_count;
    uint32_t sample_bits;
    if (!bytes || !sample_rate || !channels || !packet_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "ima4", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&rate, &sample_rate_bits, sizeof(rate));
    flags = qw_read_u32_be(bytes + 12u);
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    frames_per_packet = qw_read_u32_be(bytes + 20u);
    channel_count = qw_read_u32_be(bytes + 24u);
    sample_bits = qw_read_u32_be(bytes + 28u);
    if (!(rate >= 8000.0 && rate <= 384000.0) ||
        flags != 0u || channel_count < 1u || channel_count > 2u ||
        sample_bits != 0u || frames_per_packet != CAF_IMA4_FRAMES_PER_PACKET ||
        bytes_per_packet != channel_count * CAF_IMA4_BYTES_PER_CHANNEL) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *sample_rate = (uint32_t)rate;
    if ((double)*sample_rate != rate) return QW_STATUS_ERR_UNSUPPORTED;
    *channels = (uint16_t)channel_count;
    *packet_size = bytes_per_packet;
    return QW_STATUS_OK;
}

int qw_audio_caf_ima4_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafIma4Source *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks;
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t packet_frame_count;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_ima4_parse_description(chunks.description,
                                    chunks.description_size,
                                    &source->sample_rate,
                                    &source->channels,
                                    &source->packet_size);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !source->packet_size ||
        chunks.audio_data_size == 0u ||
        chunks.audio_data_size % source->packet_size != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->packet_count = chunks.audio_data_size / source->packet_size;
    if (!source->packet_count ||
        (uint64_t)source->packet_count >
            UINT64_MAX / CAF_IMA4_FRAMES_PER_PACKET) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_frame_count = (uint64_t)source->packet_count *
                         CAF_IMA4_FRAMES_PER_PACKET;
    packet_table.valid_frame_count = packet_frame_count;
    if (chunks.packet_table) {
        if (chunks.packet_table_size !=
            QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_table.packet_count != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    }
    rc = qw_audio_caf_packet_table_validate_frames(
        &packet_table, source->packet_count, CAF_IMA4_FRAMES_PER_PACKET);
    if (rc != QW_STATUS_OK) return rc;
    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_ima4_next_frame(const QwAudioCafIma4Source *source,
                                 size_t packet_index,
                                 QwAudioCafIma4Frame *frame)
{
    size_t offset;
    if (!source || !frame || !source->data || !source->packet_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (packet_index == source->packet_count) return 0;
    if (packet_index > source->packet_count ||
        packet_index > SIZE_MAX / source->packet_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    offset = packet_index * source->packet_size;
    if (offset > source->data_size ||
        source->packet_size > source->data_size - offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->data + offset;
    frame->size = source->packet_size;
    frame->next_packet_index = packet_index + 1u;
    frame->sample_count = CAF_IMA4_FRAMES_PER_PACKET;
    return 1;
}
