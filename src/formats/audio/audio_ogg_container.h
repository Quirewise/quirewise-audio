#ifndef QUIREWISE_AUDIO_OGG_CONTAINER_H
#define QUIREWISE_AUDIO_OGG_CONTAINER_H

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_OGG_FLAG_CONTINUED = 0x01u,
    QW_AUDIO_OGG_FLAG_BOS = 0x02u,
    QW_AUDIO_OGG_FLAG_EOS = 0x04u
};

typedef struct QwAudioOggPage {
    size_t offset;
    size_t body_offset;
    size_t body_size;
    size_t next_offset;
    const uint8_t *lacing;
    uint64_t granule_position;
    uint32_t serial_number;
    uint32_t sequence_number;
    uint8_t segment_count;
    uint8_t header_type;
} QwAudioOggPage;

typedef struct QwAudioOggPacketCursor {
    const uint8_t *data;
    size_t size;
    size_t page_position;
    size_t segment_index;
    size_t body_position;
    uint32_t serial_number;
    uint32_t expected_sequence;
    QwAudioOggPage page;
    int first_page;
    int page_loaded;
    int eos_seen;
} QwAudioOggPacketCursor;

typedef struct QwAudioOggPacketBuffer {
    uint8_t *data;
    size_t capacity;
} QwAudioOggPacketBuffer;

typedef struct QwAudioOggPacket {
    const uint8_t *data;
    size_t size;
    size_t first_page_offset;
    size_t page_offset;
    size_t first_segment_index;
    size_t page_segment_count;
    uint64_t granule_position;
    uint32_t sequence_number;
    uint8_t page_flags;
    int crossed_pages;
    int last_packet_on_page;
} QwAudioOggPacket;

int qw_audio_ogg_page_parse(const uint8_t *bytes,
                            size_t size,
                            size_t offset,
                            QwAudioOggPage *page);
int qw_audio_ogg_packet_cursor_init(QwAudioOggPacketCursor *cursor,
                                    const uint8_t *bytes,
                                    size_t size);
int qw_audio_ogg_packet_next(QwAudioOggPacketCursor *cursor,
                             QwAudioOggPacketBuffer *packet_buffer,
                             QwAudioOggPacket *packet);

#endif
