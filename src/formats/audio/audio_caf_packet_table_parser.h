#ifndef QUIREWISE_AUDIO_CAF_PACKET_TABLE_PARSER_H
#define QUIREWISE_AUDIO_CAF_PACKET_TABLE_PARSER_H

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE = 24u };

typedef struct QwAudioCafPacketTableHeader {
    uint64_t packet_count;
    uint64_t valid_frame_count;
    uint32_t priming_frames;
    uint32_t remainder_frames;
} QwAudioCafPacketTableHeader;

int qw_audio_caf_packet_table_parse_header(
    const uint8_t *bytes,
    size_t size,
    QwAudioCafPacketTableHeader *header);

int qw_audio_caf_packet_table_validate_frames(
    const QwAudioCafPacketTableHeader *header,
    uint64_t packet_count,
    uint32_t frames_per_packet);

#endif
