#include "audio_voc_blocks.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    AUDIO_VOC_HEADER_SIZE = 26u
};

int qw_audio_voc_container_parse_header(const uint8_t *bytes,
                                        size_t size,
                                        size_t *data_offset)
{
    uint16_t header_offset;
    uint16_t version;
    uint16_t checksum;
    if (!bytes || !data_offset) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size < AUDIO_VOC_HEADER_SIZE ||
        memcmp(bytes, "Creative Voice File\x1a", 20u) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    header_offset = qw_read_u16_le(bytes + 20u);
    version = qw_read_u16_le(bytes + 22u);
    checksum = qw_read_u16_le(bytes + 24u);
    if (header_offset < AUDIO_VOC_HEADER_SIZE || header_offset > size ||
        (uint16_t)(~version + 0x1234u) != checksum) {
        return QW_STATUS_ERR_FORMAT;
    }
    *data_offset = header_offset;
    return QW_STATUS_OK;
}

int qw_audio_voc_block_next(const uint8_t *bytes,
                            size_t size,
                            size_t position,
                            size_t end,
                            QwAudioVocBlock *block)
{
    size_t remaining;
    uint32_t payload_size;
    if (!bytes || !block) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(block, 0, sizeof(*block));
    if (end > size || position > end) return QW_STATUS_ERR_FORMAT;
    if (position == end) return 0;
    remaining = end - position;
    block->offset = position;
    block->type = bytes[position];
    if (block->type == QW_AUDIO_VOC_BLOCK_TERMINATOR) {
        block->next_offset = position + 1u;
        return 1;
    }
    if (remaining < 4u) return QW_STATUS_ERR_FORMAT;
    payload_size = (uint32_t)bytes[position + 1u] |
                   ((uint32_t)bytes[position + 2u] << 8u) |
                   ((uint32_t)bytes[position + 3u] << 16u);
    if ((size_t)payload_size > remaining - 4u) return QW_STATUS_ERR_FORMAT;
    block->payload = bytes + position + 4u;
    block->payload_size = (size_t)payload_size;
    block->next_offset = position + 4u + (size_t)payload_size;
    return 1;
}
