#include "audio_ogg_container.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>
#include <stdlib.h>

enum {
    AUDIO_OGG_PAGE_HEADER_SIZE = 27u,
    AUDIO_OGG_PAGE_CRC_OFFSET = 22u,
    AUDIO_OGG_VALID_FLAGS = 0x07u
};

static uint32_t audio_ogg_page_crc(const uint8_t *bytes,
                                  size_t page_offset,
                                  size_t page_size)
{
    uint32_t crc = 0u;
    size_t i;
    for (i = 0u; i < page_size; ++i) {
        uint8_t value = i >= AUDIO_OGG_PAGE_CRC_OFFSET &&
                        i < AUDIO_OGG_PAGE_CRC_OFFSET + 4u
            ? 0u : bytes[page_offset + i];
        unsigned bit;
        crc ^= (uint32_t)value << 24u;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & UINT32_C(0x80000000))
                ? (crc << 1u) ^ UINT32_C(0x04c11db7)
                : crc << 1u;
        }
    }
    return crc;
}

static int audio_ogg_packet_buffer_reserve(QwAudioOggPacketBuffer *buffer,
                                           size_t required)
{
    size_t capacity;
    uint8_t *grown;
    if (!buffer) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (required <= buffer->capacity) return QW_STATUS_OK;
    capacity = buffer->capacity ? buffer->capacity : 1024u;
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2u) {
            capacity = required;
            break;
        }
        capacity *= 2u;
    }
    grown = (uint8_t *)realloc(buffer->data, capacity);
    if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
    buffer->data = grown;
    buffer->capacity = capacity;
    return QW_STATUS_OK;
}

int qw_audio_ogg_page_parse(const uint8_t *bytes,
                            size_t size,
                            size_t offset,
                            QwAudioOggPage *page)
{
    size_t header_size;
    size_t body_size = 0u;
    size_t page_size;
    size_t i;
    int has_complete_packet = 0;
    if (!bytes || !page || offset > size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(page, 0, sizeof(*page));
    if (size - offset < AUDIO_OGG_PAGE_HEADER_SIZE ||
        memcmp(bytes + offset, "OggS", 4u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (bytes[offset + 4u] != 0u ||
        (bytes[offset + 5u] & (uint8_t)~AUDIO_OGG_VALID_FLAGS) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    page->segment_count = bytes[offset + 26u];
    header_size = AUDIO_OGG_PAGE_HEADER_SIZE + page->segment_count;
    if (header_size > size - offset) return QW_STATUS_ERR_FORMAT;
    page->lacing = bytes + offset + AUDIO_OGG_PAGE_HEADER_SIZE;
    for (i = 0u; i < page->segment_count; ++i) {
        if ((size_t)page->lacing[i] > SIZE_MAX - body_size)
            return QW_STATUS_ERR_FORMAT;
        body_size += page->lacing[i];
        if (page->lacing[i] < 255u) has_complete_packet = 1;
    }
    if (body_size > size - offset - header_size) return QW_STATUS_ERR_FORMAT;
    page_size = header_size + body_size;
    if (qw_read_u32_le(bytes + offset + AUDIO_OGG_PAGE_CRC_OFFSET) !=
        audio_ogg_page_crc(bytes, offset, page_size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    page->offset = offset;
    page->body_offset = offset + header_size;
    page->body_size = body_size;
    page->next_offset = offset + page_size;
    page->granule_position = qw_read_u64_le(bytes + offset + 6u);
    if (!has_complete_packet && page->granule_position != UINT64_MAX)
        return QW_STATUS_ERR_FORMAT;
    page->serial_number = qw_read_u32_le(bytes + offset + 14u);
    page->sequence_number = qw_read_u32_le(bytes + offset + 18u);
    page->header_type = bytes[offset + 5u];
    return QW_STATUS_OK;
}

int qw_audio_ogg_packet_cursor_init(QwAudioOggPacketCursor *cursor,
                                    const uint8_t *bytes,
                                    size_t size)
{
    if (!cursor || !bytes || !size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(cursor, 0, sizeof(*cursor));
    cursor->data = bytes;
    cursor->size = size;
    cursor->first_page = 1;
    return QW_STATUS_OK;
}

static int audio_ogg_packet_load_page(QwAudioOggPacketCursor *cursor,
                                      int packet_continues)
{
    QwAudioOggPage page;
    int rc;
    if (cursor->eos_seen) {
        return cursor->page_position == cursor->size ? 0 : QW_STATUS_ERR_FORMAT;
    }
    if (cursor->page_position >= cursor->size) {
        return packet_continues ? QW_STATUS_ERR_FORMAT : 0;
    }
    rc = qw_audio_ogg_page_parse(cursor->data, cursor->size,
                                 cursor->page_position, &page);
    if (rc != QW_STATUS_OK) return rc;
    if (cursor->first_page) {
        if ((page.header_type & QW_AUDIO_OGG_FLAG_BOS) == 0u ||
            (page.header_type & QW_AUDIO_OGG_FLAG_CONTINUED) != 0u ||
            page.sequence_number != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        cursor->serial_number = page.serial_number;
        cursor->expected_sequence = page.sequence_number + 1u;
        cursor->first_page = 0;
    } else {
        if ((page.header_type & QW_AUDIO_OGG_FLAG_BOS) != 0u ||
            ((page.header_type & QW_AUDIO_OGG_FLAG_CONTINUED) != 0u) !=
                (packet_continues != 0) ||
            page.serial_number != cursor->serial_number ||
            page.sequence_number != cursor->expected_sequence) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        ++cursor->expected_sequence;
    }
    cursor->page_position = page.next_offset;
    cursor->segment_index = 0u;
    cursor->body_position = 0u;
    cursor->page = page;
    cursor->page_loaded = 1;
    cursor->eos_seen =
        (page.header_type & QW_AUDIO_OGG_FLAG_EOS) != 0u;
    return 1;
}

int qw_audio_ogg_packet_next(QwAudioOggPacketCursor *cursor,
                             QwAudioOggPacketBuffer *packet_buffer,
                             QwAudioOggPacket *packet)
{
    const uint8_t *packet_start = NULL;
    size_t packet_start_page_offset = 0u;
    size_t packet_start_segment = 0u;
    size_t packet_size = 0u;
    int packet_continues = 0;
    int crossed_pages = 0;
    if (!cursor || !cursor->data || !packet_buffer || !packet)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(packet, 0, sizeof(*packet));

    for (;;) {
        const uint8_t *segment_data;
        size_t lace;
        int rc;
        if (!cursor->page_loaded ||
            cursor->segment_index >= cursor->page.segment_count) {
            if (cursor->page_loaded) cursor->page_loaded = 0;
            rc = audio_ogg_packet_load_page(cursor, packet_continues);
            if (rc <= 0) return rc;
        }
        if (cursor->body_position > cursor->page.body_size)
            return QW_STATUS_ERR_FORMAT;
        lace = cursor->page.lacing[cursor->segment_index];
        if (lace > cursor->page.body_size - cursor->body_position)
            return QW_STATUS_ERR_FORMAT;
        segment_data = cursor->data + cursor->page.body_offset +
                       cursor->body_position;
        if (lace > SIZE_MAX - packet_size) return QW_STATUS_ERR_FORMAT;
        if (!packet_continues) {
            packet_start = segment_data;
            packet_start_page_offset = cursor->page.offset;
            packet_start_segment = cursor->segment_index;
        }
        if (crossed_pages) {
            size_t required = packet_size + lace;
            int reserve_rc = audio_ogg_packet_buffer_reserve(packet_buffer,
                                                              required);
            if (reserve_rc != QW_STATUS_OK) return reserve_rc;
            if (lace) memcpy(packet_buffer->data + packet_size,
                             segment_data, lace);
        }
        packet_size += lace;
        cursor->body_position += lace;
        ++cursor->segment_index;
        packet_continues = lace == 255u;
        if (!packet_continues) {
            size_t later_segment;
            int has_later_complete_packet = 0;
            packet->data = crossed_pages ? packet_buffer->data : packet_start;
            packet->size = packet_size;
            packet->first_page_offset = packet_start_page_offset;
            packet->page_offset = cursor->page.offset;
            packet->first_segment_index = packet_start_segment;
            packet->page_segment_count = cursor->page.segment_count;
            packet->granule_position = cursor->page.granule_position;
            packet->sequence_number = cursor->page.sequence_number;
            packet->page_flags = cursor->page.header_type;
            packet->crossed_pages = crossed_pages;
            for (later_segment = cursor->segment_index;
                 later_segment < cursor->page.segment_count;
                 ++later_segment) {
                if (cursor->page.lacing[later_segment] < 255u) {
                    has_later_complete_packet = 1;
                    break;
                }
            }
            if (cursor->eos_seen &&
                cursor->segment_index < cursor->page.segment_count &&
                !has_later_complete_packet) {
                return QW_STATUS_ERR_FORMAT;
            }
            packet->last_packet_on_page = !has_later_complete_packet;
            return 1;
        }
        if (cursor->segment_index >= cursor->page.segment_count) {
            if (cursor->eos_seen) return QW_STATUS_ERR_FORMAT;
            if (!crossed_pages) {
                rc = audio_ogg_packet_buffer_reserve(packet_buffer,
                                                     packet_size);
                if (rc != QW_STATUS_OK) return rc;
                if (packet_size) memcpy(packet_buffer->data, packet_start,
                                        packet_size);
                crossed_pages = 1;
            }
        }
    }
}
