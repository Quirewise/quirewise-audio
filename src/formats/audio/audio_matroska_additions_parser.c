#include "audio_matroska_additions_parser.h"

#include "audio_matroska_ebml_parser.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    MATROSKA_ID_BLOCK_MORE = 0xa6u,
    MATROSKA_ID_BLOCK_ADD_ID = 0xeeu,
    MATROSKA_ID_BLOCK_ADDITIONAL = 0xa5u
};

static int audio_matroska_addition_parse_more(
        const uint8_t *data,
        size_t size,
        QwAudioMatroskaBlockAddition *addition)
{
    size_t position = 0u;
    int has_id = 0;
    int has_data = 0;

    memset(addition, 0, sizeof(*addition));
    addition->id = 1u;
    while (position < size) {
        QwAudioMatroskaEbmlElement element;
        int rc = qw_audio_matroska_ebml_read_element(
            data, size, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_BLOCK_ADD_ID) {
            if (has_id) return QW_STATUS_ERR_FORMAT;
            has_id = 1;
            rc = qw_audio_matroska_ebml_read_uint(
                data + element.payload, element.size, &addition->id);
            if (rc != QW_STATUS_OK || !addition->id) return QW_STATUS_ERR_FORMAT;
        } else if (element.id == MATROSKA_ID_BLOCK_ADDITIONAL) {
            if (has_data) return QW_STATUS_ERR_FORMAT;
            has_data = 1;
            addition->data = data + element.payload;
            addition->size = element.size;
        }
        position = element.next;
    }
    if (position != size || !has_data) return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}

int qw_audio_matroska_additions_parse(
        const uint8_t *data,
        size_t size,
        QwAudioMatroskaBlockAdditions *additions)
{
    size_t position = 0u;

    if (!data || !size || !additions) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(additions, 0, sizeof(*additions));
    while (position < size) {
        QwAudioMatroskaEbmlElement element;
        int rc = qw_audio_matroska_ebml_read_element(
            data, size, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_BLOCK_MORE) {
            if (additions->count >= QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS)
                return QW_STATUS_ERR_UNSUPPORTED;
            rc = audio_matroska_addition_parse_more(
                data + element.payload, element.size,
                &additions->items[additions->count]);
            if (rc != QW_STATUS_OK) return rc;
            ++additions->count;
        }
        position = element.next;
    }
    if (position != size || !additions->count) return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
