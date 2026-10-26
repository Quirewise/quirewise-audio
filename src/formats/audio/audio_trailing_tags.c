#include "audio_trailing_tags.h"

#include "../../core/byte_utils.h"
#include "audio_id3_tag.h"
#include "quirewise/status.h"

#include <string.h>

enum { AUDIO_APEV2_FOOTER_SIZE = 32u };

int qw_audio_trailing_tags_audio_end(const uint8_t *bytes,
                                    size_t size,
                                    size_t *audio_end)
{
    size_t end;

    if (!bytes || !audio_end) return QW_STATUS_ERR_INVALID_ARGUMENT;
    end = qw_audio_id3v1_audio_end(bytes, size, NULL);
    if (end >= AUDIO_APEV2_FOOTER_SIZE &&
        memcmp(bytes + end - AUDIO_APEV2_FOOTER_SIZE,
               "APETAGEX", 8u) == 0) {
        const uint8_t *footer = bytes + end - AUDIO_APEV2_FOOTER_SIZE;
        uint32_t tag_size = qw_read_u32_le(footer + 12u);
        uint32_t flags = qw_read_u32_le(footer + 20u);
        size_t total_size;

        if (tag_size < AUDIO_APEV2_FOOTER_SIZE || tag_size > end)
            return QW_STATUS_ERR_FORMAT;
        total_size = (size_t)tag_size;
        if (flags & UINT32_C(0x80000000)) {
            if (total_size > SIZE_MAX - AUDIO_APEV2_FOOTER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            total_size += AUDIO_APEV2_FOOTER_SIZE;
        }
        if (total_size > end) return QW_STATUS_ERR_FORMAT;
        end -= total_size;
    }
    *audio_end = end;
    return QW_STATUS_OK;
}
