#include "audio_matroska_ebml_parser.h"

#include "quirewise/status.h"

static unsigned audio_matroska_ebml_vint_length(uint8_t first_byte)
{
    uint8_t marker = 0x80u;
    unsigned length = 1u;
    while (length <= 8u && (first_byte & marker) == 0u) {
        marker >>= 1u;
        ++length;
    }
    return length <= 8u ? length : 0u;
}

int qw_audio_matroska_ebml_read_vint(const uint8_t *data,
                                    size_t size,
                                    size_t *position,
                                    int keep_marker,
                                    uint64_t *value,
                                    unsigned *length_out)
{
    unsigned length;
    unsigned index;
    uint8_t marker;
    uint64_t result;

    if (!data || !position || !value || *position >= size)
        return QW_STATUS_ERR_FORMAT;
    length = audio_matroska_ebml_vint_length(data[*position]);
    if (!length || (size_t)length > size - *position)
        return QW_STATUS_ERR_FORMAT;
    marker = (uint8_t)(0x80u >> (length - 1u));
    result = keep_marker ? data[*position]
                         : (data[*position] & (uint8_t)~marker);
    for (index = 1u; index < length; ++index) {
        if (result > (UINT64_MAX >> 8u)) return QW_STATUS_ERR_FORMAT;
        result = (result << 8u) | data[*position + index];
    }
    *position += length;
    *value = result;
    if (length_out) *length_out = length;
    return QW_STATUS_OK;
}

int qw_audio_matroska_ebml_read_element(
        const uint8_t *data,
        size_t parent_end,
        size_t position,
        QwAudioMatroskaEbmlElement *element)
{
    uint64_t raw_id;
    uint64_t raw_size;
    uint64_t unknown_size_marker;
    unsigned id_length;
    unsigned size_length;
    size_t cursor = position;
    int rc;

    if (!data || !element || position >= parent_end)
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_matroska_ebml_read_vint(data, parent_end, &cursor, 1,
                                          &raw_id, &id_length);
    if (rc != QW_STATUS_OK || id_length > 4u || raw_id > UINT32_MAX)
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_matroska_ebml_read_vint(data, parent_end, &cursor, 0,
                                          &raw_size, &size_length);
    if (rc != QW_STATUS_OK) return rc;

    unknown_size_marker = (UINT64_C(1) << (7u * size_length)) - 1u;
    element->id = (uint32_t)raw_id;
    element->payload = cursor;
    element->unknown_size = raw_size == unknown_size_marker;
    if (element->unknown_size) {
        element->size = parent_end - cursor;
        element->next = parent_end;
    } else {
        if (raw_size > (uint64_t)(parent_end - cursor) ||
            raw_size > SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        element->size = (size_t)raw_size;
        element->next = cursor + element->size;
    }
    return QW_STATUS_OK;
}

int qw_audio_matroska_ebml_read_uint(const uint8_t *data,
                                    size_t size,
                                    uint64_t *value)
{
    size_t index;
    uint64_t result = 0u;

    if (!data || !value || !size || size > 8u) return QW_STATUS_ERR_FORMAT;
    for (index = 0u; index < size; ++index)
        result = (result << 8u) | data[index];
    *value = result;
    return QW_STATUS_OK;
}
