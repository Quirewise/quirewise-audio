#include "audio_caf_chunks.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_HEADER_SIZE = 8u,
    CAF_CHUNK_HEADER_SIZE = 12u,
    CAF_DATA_EDIT_COUNT_SIZE = 4u
};

int qw_audio_caf_parse_chunks(const uint8_t *bytes,
                              size_t size,
                              QwAudioCafChunks *chunks)
{
    size_t offset;
    int description_seen = 0;
    int data_seen = 0;
    int packet_table_seen = 0;
    int magic_cookie_seen = 0;
    int channel_layout_seen = 0;
    if (!bytes || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(chunks, 0, sizeof(*chunks));
    if (size < CAF_HEADER_SIZE || memcmp(bytes, "caff", 4u) != 0)
        return QW_STATUS_ERR_FORMAT;
    if (qw_read_u16_be(bytes + 4u) != 1u) return QW_STATUS_ERR_UNSUPPORTED;

    offset = CAF_HEADER_SIZE;
    while (offset < size) {
        const uint8_t *chunk;
        const uint8_t *payload;
        uint64_t encoded_size;
        size_t payload_offset;
        size_t payload_size;
        size_t chunk_end;
        if (size - offset < CAF_CHUNK_HEADER_SIZE) return QW_STATUS_ERR_FORMAT;
        chunk = bytes + offset;
        encoded_size = qw_read_u64_be(chunk + 4u);
        payload_offset = offset + CAF_CHUNK_HEADER_SIZE;
        payload = bytes + payload_offset;
        if (encoded_size == UINT64_MAX) {
            if (memcmp(chunk, "data", 4u) != 0)
                return QW_STATUS_ERR_FORMAT;
            payload_size = size - payload_offset;
            chunk_end = size;
        } else {
            if (encoded_size > (uint64_t)(size - payload_offset))
                return QW_STATUS_ERR_FORMAT;
            payload_size = (size_t)encoded_size;
            chunk_end = payload_offset + payload_size;
        }

        if (memcmp(chunk, "desc", 4u) == 0) {
            if (description_seen || offset != CAF_HEADER_SIZE ||
                encoded_size == UINT64_MAX) {
                return QW_STATUS_ERR_FORMAT;
            }
            chunks->description = payload;
            chunks->description_size = payload_size;
            description_seen = 1;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            if (!description_seen || data_seen ||
                payload_size < CAF_DATA_EDIT_COUNT_SIZE) {
                return QW_STATUS_ERR_FORMAT;
            }
            chunks->data_edit_count = qw_read_u32_be(payload);
            chunks->audio_data = payload + CAF_DATA_EDIT_COUNT_SIZE;
            chunks->audio_data_size = payload_size - CAF_DATA_EDIT_COUNT_SIZE;
            data_seen = 1;
        } else if (memcmp(chunk, "pakt", 4u) == 0) {
            if (packet_table_seen || encoded_size == UINT64_MAX)
                return QW_STATUS_ERR_FORMAT;
            chunks->packet_table = payload;
            chunks->packet_table_size = payload_size;
            packet_table_seen = 1;
            chunks->has_metadata = 1;
        } else if (memcmp(chunk, "kuki", 4u) == 0) {
            if (magic_cookie_seen || encoded_size == UINT64_MAX)
                return QW_STATUS_ERR_FORMAT;
            chunks->magic_cookie = payload;
            chunks->magic_cookie_size = payload_size;
            magic_cookie_seen = 1;
            chunks->has_metadata = 1;
        } else if (memcmp(chunk, "chan", 4u) == 0) {
            if (channel_layout_seen || encoded_size == UINT64_MAX)
                return QW_STATUS_ERR_FORMAT;
            chunks->channel_layout = payload;
            chunks->channel_layout_size = payload_size;
            channel_layout_seen = 1;
            chunks->has_metadata = 1;
        } else {
            if (encoded_size == UINT64_MAX) return QW_STATUS_ERR_FORMAT;
            chunks->has_metadata = 1;
        }
        offset = chunk_end;
    }
    if (offset != size || !description_seen || !data_seen)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
