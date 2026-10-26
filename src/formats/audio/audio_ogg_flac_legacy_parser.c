#include "audio_ogg_flac_legacy_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    AUDIO_OGG_FLAC_LEGACY_METADATA_HEADER_SIZE = 4u,
    AUDIO_OGG_FLAC_LEGACY_METADATA_STREAMINFO = 0u,
    AUDIO_OGG_FLAC_LEGACY_METADATA_RESERVED = 127u
};

static uint32_t audio_ogg_flac_legacy_read_u24_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 16u) |
           ((uint32_t)bytes[1] << 8u) | bytes[2];
}

static int audio_ogg_flac_legacy_is_frame(const uint8_t *bytes, size_t size)
{
    return bytes && size >= 5u && bytes[0] == 0xffu &&
           (bytes[1] & 0xfcu) == 0xf8u && (bytes[1] & 0x02u) == 0u;
}

static int audio_ogg_flac_legacy_validate_metadata(
        const QwAudioOggPacket *packet)
{
    uint8_t type;
    uint32_t block_size;

    if (!packet || !packet->data ||
        packet->size < AUDIO_OGG_FLAC_LEGACY_METADATA_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    type = packet->data[0] & 0x7fu;
    block_size = audio_ogg_flac_legacy_read_u24_be(packet->data + 1u);
    if ((size_t)block_size !=
        packet->size - AUDIO_OGG_FLAC_LEGACY_METADATA_HEADER_SIZE ||
        type == AUDIO_OGG_FLAC_LEGACY_METADATA_STREAMINFO ||
        type == AUDIO_OGG_FLAC_LEGACY_METADATA_RESERVED) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_ogg_flac_legacy_parse_headers(
        const uint8_t *bytes,
        size_t size,
        QwAudioFlacSource *flac,
        QwAudioOggPacketBuffer *packet_buffer,
        QwAudioOggPacketCursor *first_audio_cursor)
{
    QwAudioOggPacketCursor cursor;
    QwAudioOggPacket packet;
    uint32_t streaminfo_size;
    int rc;

    if (!bytes || !size || !flac || !packet_buffer || !first_audio_cursor)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(flac, 0, sizeof(*flac));
    rc = qw_audio_ogg_packet_cursor_init(&cursor, bytes, size);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    if (packet.size != 4u || memcmp(packet.data, "fLaC", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    if ((packet.page_flags & QW_AUDIO_OGG_FLAG_BOS) == 0u ||
        packet.page_segment_count != 1u || !packet.last_packet_on_page ||
        packet.granule_position != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }

    rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    if (packet.granule_position != 0u || packet.size < 4u ||
        (packet.data[0] & 0x7fu) !=
            AUDIO_OGG_FLAC_LEGACY_METADATA_STREAMINFO) {
        return QW_STATUS_ERR_FORMAT;
    }
    streaminfo_size = audio_ogg_flac_legacy_read_u24_be(packet.data + 1u);
    if (streaminfo_size != QW_AUDIO_FLAC_STREAMINFO_SIZE ||
        packet.size != AUDIO_OGG_FLAC_LEGACY_METADATA_HEADER_SIZE +
                       (size_t)streaminfo_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_flac_parse_streaminfo(
        packet.data + AUDIO_OGG_FLAC_LEGACY_METADATA_HEADER_SIZE,
        streaminfo_size, flac);
    if (rc != QW_STATUS_OK) return rc;
    flac->data = bytes;
    flac->size = size;

    for (;;) {
        QwAudioOggPacketCursor before_packet = cursor;
        rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (audio_ogg_flac_legacy_is_frame(packet.data, packet.size)) {
            *first_audio_cursor = before_packet;
            return QW_STATUS_OK;
        }
        if (packet.granule_position != 0u) return QW_STATUS_ERR_FORMAT;
        rc = audio_ogg_flac_legacy_validate_metadata(&packet);
        if (rc != QW_STATUS_OK) return rc;
    }
}
