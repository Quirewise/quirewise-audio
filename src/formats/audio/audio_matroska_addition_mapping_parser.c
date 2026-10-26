#include "audio_matroska_addition_mapping_parser.h"

#include "audio_matroska_ebml_parser.h"
#include "quirewise/status.h"

enum {
    MATROSKA_ID_BLOCK_ADD_ID_VALUE = 0x41f0u,
    MATROSKA_ID_BLOCK_ADD_ID_TYPE = 0x41e7u
};

int qw_audio_matroska_addition_mapping_parse(const uint8_t *data,
                                             size_t size,
                                             QwAudioMatroskaAdditionMapping *mapping)
{
    size_t position = 0u;
    uint64_t parsed_type = 0u;
    uint64_t parsed_value = 0u;
    int has_type = 0;
    int has_value = 0;

    if (!data || !size || !mapping)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    while (position < size) {
        QwAudioMatroskaEbmlElement element;
        int rc = qw_audio_matroska_ebml_read_element(
            data, size, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_BLOCK_ADD_ID_TYPE) {
            if (has_type) return QW_STATUS_ERR_FORMAT;
            has_type = 1;
            rc = qw_audio_matroska_ebml_read_uint(
                data + element.payload, element.size, &parsed_type);
            if (rc != QW_STATUS_OK) return rc;
        } else if (element.id == MATROSKA_ID_BLOCK_ADD_ID_VALUE) {
            if (has_value) return QW_STATUS_ERR_FORMAT;
            has_value = 1;
            rc = qw_audio_matroska_ebml_read_uint(
                data + element.payload, element.size, &parsed_value);
            if (rc != QW_STATUS_OK || parsed_value < 2u) return QW_STATUS_ERR_FORMAT;
        }
        position = element.next;
    }
    if (position != size || !has_type) return QW_STATUS_ERR_FORMAT;
    mapping->block_add_id_type = parsed_type;
    mapping->block_add_id_value = parsed_value;
    mapping->has_block_add_id_value = has_value;
    return QW_STATUS_OK;
}
