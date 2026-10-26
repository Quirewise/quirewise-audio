#include "audio_id3_tag.h"

#include <string.h>

enum {
    ID3_HEADER_SIZE = 10u,
    ID3_V1_SIZE = 128u
};

int qw_audio_id3_skip_leading_v2(const uint8_t *bytes,
                                 size_t size,
                                 size_t *skip_bytes)
{
    size_t tag_size;
    size_t total_size;
    unsigned i;
    if (!bytes || !skip_bytes) return 0;
    *skip_bytes = 0u;
    if (size < 3u || memcmp(bytes, "ID3", 3u) != 0) return 1;
    if (size < ID3_HEADER_SIZE) return 0;
    if (bytes[3] < 2u || bytes[3] > 4u || bytes[4] == 0xffu) return 0;
    tag_size = 0u;
    for (i = 6u; i < ID3_HEADER_SIZE; ++i) {
        if ((bytes[i] & 0x80u) != 0u) return 0;
        tag_size = (tag_size << 7u) | bytes[i];
    }
    if (tag_size > SIZE_MAX - ID3_HEADER_SIZE) return 0;
    total_size = ID3_HEADER_SIZE + tag_size;
    if (bytes[3] == 4u && (bytes[5] & 0x10u) != 0u) {
        const uint8_t *footer;
        if (total_size > size || size - total_size < ID3_HEADER_SIZE ||
            total_size > SIZE_MAX - ID3_HEADER_SIZE) return 0;
        footer = bytes + total_size;
        if (memcmp(footer, "3DI", 3u) != 0 ||
            memcmp(footer + 3u, bytes + 3u, 7u) != 0) return 0;
        total_size += ID3_HEADER_SIZE;
    }
    if (total_size > size) return 0;
    *skip_bytes = total_size;
    return 1;
}

size_t qw_audio_id3v1_audio_end(const uint8_t *bytes,
                                size_t size,
                                int *has_tag)
{
    if (has_tag) *has_tag = 0;
    if (!bytes || size < ID3_V1_SIZE ||
        memcmp(bytes + size - ID3_V1_SIZE, "TAG", 3u) != 0) {
        return size;
    }
    if (has_tag) *has_tag = 1;
    return size - ID3_V1_SIZE;
}
