#ifndef QUIREWISE_AUDIO_ASF_PARSER_H
#define QUIREWISE_AUDIO_ASF_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAsfSource {
    const uint8_t *data;
    size_t data_end;
    size_t packet_position;
    size_t packet_cursor;
    size_t packet_payload_end;
    size_t pending_subpayload_position;
    size_t pending_subpayload_end;
    uint64_t data_packet_count;
    uint64_t data_packets_read;
    uint64_t sample_count;
    uint64_t packet_index;
    uint64_t active_object_number;
    uint64_t object_size;
    uint64_t object_received;
    size_t object_capacity;
    size_t audio_track_count;
    const uint8_t *extradata;
    size_t extradata_size;
    uint8_t *object_data;
    uint32_t sample_rate;
    uint32_t average_bytes_per_second;
    uint32_t minimum_packet_size;
    uint32_t maximum_packet_size;
    uint32_t preroll_ms;
    uint16_t channels;
    uint16_t block_align;
    uint16_t bits_per_sample;
    uint16_t format_tag;
    uint16_t descramble_packet_size;
    uint16_t descramble_chunk_size;
    uint8_t stream_number;
    uint8_t descramble_span;
    uint8_t packet_payloads_remaining;
    uint8_t packet_payload_length_type;
    uint8_t packet_property;
    uint8_t packet_active;
    uint8_t object_active;
    uint8_t is_compressed_payload;
} QwAudioAsfSource;

typedef struct QwAudioAsfPacket {
    const uint8_t *data;
    size_t size;
} QwAudioAsfPacket;

/* Parses the ASF header and selects a one-based audio stream. Zero selects 1. */
int qw_audio_asf_parse(const uint8_t *data,
                       size_t size,
                       size_t requested_audio_track,
                       QwAudioAsfSource *source);

/* Returns 1 with a WMA packet, 0 at stream end, or a PAGE error. */
int qw_audio_asf_next_packet(QwAudioAsfSource *source,
                             QwAudioAsfPacket *packet);

void qw_audio_asf_dispose(QwAudioAsfSource *source);

#endif
