#include "audio_mpeg4_esds_parser.h"

#include "quirewise/status.h"

#include <string.h>

typedef struct AudioMpeg4Descriptor {
    const uint8_t *payload;
    size_t payload_size;
    uint8_t tag;
} AudioMpeg4Descriptor;

static int audio_mpeg4_descriptor_read(const uint8_t *data,
                                       size_t size,
                                       size_t *position,
                                       AudioMpeg4Descriptor *descriptor)
{
    uint32_t payload_size = 0u;
    unsigned length_bytes;
    int terminated = 0;

    if (!data || !position || !descriptor || *position >= size)
        return QW_STATUS_ERR_FORMAT;
    descriptor->tag = data[(*position)++];
    for (length_bytes = 0u; length_bytes < 4u; ++length_bytes) {
        uint8_t byte;
        if (*position >= size || payload_size > (UINT32_MAX >> 7u))
            return QW_STATUS_ERR_FORMAT;
        byte = data[(*position)++];
        payload_size = (payload_size << 7u) | (uint32_t)(byte & 0x7fu);
        if ((byte & 0x80u) == 0u) {
            terminated = 1;
            break;
        }
    }
    if (!terminated || (size_t)payload_size > size - *position)
        return QW_STATUS_ERR_FORMAT;
    descriptor->payload = data + *position;
    descriptor->payload_size = (size_t)payload_size;
    *position += descriptor->payload_size;
    return QW_STATUS_OK;
}

static int audio_mpeg4_is_zero_padding(const uint8_t *data, size_t size)
{
    size_t i;
    for (i = 0u; i < size; ++i) {
        if (data[i] != 0u) return 0;
    }
    return 1;
}

static uint32_t audio_mpeg4_read_u32_be(const uint8_t *data)
{
    return ((uint32_t)data[0] << 24u) |
           ((uint32_t)data[1] << 16u) |
           ((uint32_t)data[2] << 8u) |
           (uint32_t)data[3];
}

static int audio_mpeg4_find_specific_info_in_decoder_config(
    const AudioMpeg4Descriptor *decoder_config,
    unsigned flags,
    const uint8_t **specific_info,
    size_t *specific_info_size,
    int *found)
{
    size_t position = 13u;

    if (decoder_config->payload_size < position) return QW_STATUS_ERR_FORMAT;
    if (decoder_config->payload[0] != 0x40u ||
        ((decoder_config->payload[1] >> 2u) & 0x3fu) != 0x05u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    while (position < decoder_config->payload_size) {
        AudioMpeg4Descriptor child;
        int rc;
        if ((flags & QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING) != 0u &&
            audio_mpeg4_is_zero_padding(decoder_config->payload + position,
                                        decoder_config->payload_size - position)) {
            break;
        }
        rc = audio_mpeg4_descriptor_read(decoder_config->payload,
                                        decoder_config->payload_size,
                                        &position, &child);
        if (rc != QW_STATUS_OK) return rc;
        if (child.tag != 0x05u) continue;
        if (child.payload_size == 0u || *found) return QW_STATUS_ERR_FORMAT;
        *specific_info = child.payload;
        *specific_info_size = child.payload_size;
        *found = 1;
    }
    return QW_STATUS_OK;
}

static int audio_mpeg4_find_specific_info_in_es(
    const AudioMpeg4Descriptor *es,
    unsigned flags,
    const uint8_t **specific_info,
    size_t *specific_info_size,
    int *found)
{
    size_t position = 3u;
    uint8_t es_flags;

    if (es->payload_size < position) return QW_STATUS_ERR_FORMAT;
    es_flags = es->payload[2];
    if ((es_flags & 0x1fu) != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    if ((es_flags & 0x80u) != 0u) {
        if (es->payload_size - position < 2u) return QW_STATUS_ERR_FORMAT;
        position += 2u;
    }
    if ((es_flags & 0x40u) != 0u) {
        size_t url_size;
        if (position >= es->payload_size) return QW_STATUS_ERR_FORMAT;
        url_size = es->payload[position++];
        if (url_size > es->payload_size - position) return QW_STATUS_ERR_FORMAT;
        position += url_size;
    }
    if ((es_flags & 0x20u) != 0u) {
        if (es->payload_size - position < 2u) return QW_STATUS_ERR_FORMAT;
        position += 2u;
    }

    while (position < es->payload_size) {
        AudioMpeg4Descriptor child;
        int rc;
        if ((flags & QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING) != 0u &&
            audio_mpeg4_is_zero_padding(es->payload + position,
                                        es->payload_size - position)) {
            break;
        }
        rc = audio_mpeg4_descriptor_read(es->payload, es->payload_size,
                                        &position, &child);
        if (rc != QW_STATUS_OK) return rc;
        if (child.tag != 0x04u) continue;
        rc = audio_mpeg4_find_specific_info_in_decoder_config(
            &child, flags, specific_info, specific_info_size, found);
        if (rc != QW_STATUS_OK) return rc;
    }
    return QW_STATUS_OK;
}

int qw_audio_mpeg4_esds_find_aac_specific_info(
    const uint8_t *data,
    size_t size,
    unsigned flags,
    const uint8_t **specific_info,
    size_t *specific_info_size)
{
    size_t position = 0u;
    int found = 0;

    if (!data || !specific_info || !specific_info_size ||
        (flags & ~(QW_AUDIO_MPEG4_ESDS_ALLOW_ATOM |
                   QW_AUDIO_MPEG4_ESDS_ALLOW_FULLBOX |
                   QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING)) != 0u) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *specific_info = NULL;
    *specific_info_size = 0u;

    if ((flags & QW_AUDIO_MPEG4_ESDS_ALLOW_ATOM) != 0u && size >= 8u &&
        memcmp(data + 4u, "esds", 4u) == 0) {
        if ((size_t)audio_mpeg4_read_u32_be(data) != size)
            return QW_STATUS_ERR_FORMAT;
        data += 8u;
        size -= 8u;
    }
    if ((flags & QW_AUDIO_MPEG4_ESDS_ALLOW_FULLBOX) != 0u && size >= 4u &&
        data[0] == 0u && data[1] == 0u && data[2] == 0u && data[3] == 0u) {
        data += 4u;
        size -= 4u;
    }

    while (position < size) {
        AudioMpeg4Descriptor descriptor;
        int rc;
        if ((flags & QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING) != 0u &&
            audio_mpeg4_is_zero_padding(data + position, size - position)) {
            break;
        }
        rc = audio_mpeg4_descriptor_read(data, size, &position, &descriptor);
        if (rc != QW_STATUS_OK) return rc;
        if (descriptor.tag != 0x03u) continue;
        rc = audio_mpeg4_find_specific_info_in_es(
            &descriptor, flags, specific_info, specific_info_size, &found);
        if (rc != QW_STATUS_OK) return rc;
    }
    return found ? QW_STATUS_OK : QW_STATUS_ERR_UNSUPPORTED;
}
