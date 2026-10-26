#include "audio_caf_packet_table_parser.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>

int qw_audio_caf_packet_table_parse_header(
    const uint8_t *bytes,
    size_t size,
    QwAudioCafPacketTableHeader *header)
{
    if (!bytes || !header) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
        return QW_STATUS_ERR_FORMAT;

    header->packet_count = qw_read_u64_be(bytes);
    header->valid_frame_count = qw_read_u64_be(bytes + 8u);
    header->priming_frames = qw_read_u32_be(bytes + 16u);
    header->remainder_frames = qw_read_u32_be(bytes + 20u);
    if (header->packet_count > (uint64_t)INT64_MAX ||
        header->valid_frame_count > (uint64_t)INT64_MAX ||
        header->priming_frames > (uint32_t)INT32_MAX ||
        header->remainder_frames > (uint32_t)INT32_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_caf_packet_table_validate_frames(
    const QwAudioCafPacketTableHeader *header,
    uint64_t packet_count,
    uint32_t frames_per_packet)
{
    uint64_t packet_frames;
    uint64_t valid_frames_with_priming;

    if (!header || !packet_count || !frames_per_packet ||
        header->valid_frame_count == 0u ||
        header->remainder_frames >= frames_per_packet ||
        packet_count > UINT64_MAX / frames_per_packet) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_frames = packet_count * frames_per_packet;
    if (header->valid_frame_count > UINT64_MAX - header->priming_frames)
        return QW_STATUS_ERR_FORMAT;
    valid_frames_with_priming =
        header->valid_frame_count + header->priming_frames;
    if (valid_frames_with_priming > UINT64_MAX - header->remainder_frames ||
        valid_frames_with_priming + header->remainder_frames != packet_frames) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}
