#include "audio_ogg_flac_parser.h"
#include "audio_ogg_flac_legacy_parser.h"

#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

enum {
    AUDIO_OGG_FLAC_MAPPING_PACKET_SIZE = 51u,
    AUDIO_OGG_FLAC_MAPPING_MAJOR = 1u,
    AUDIO_OGG_FLAC_MAPPING_MINOR = 0u,
    AUDIO_OGG_FLAC_STREAMINFO_OFFSET = 17u,
    AUDIO_OGG_FLAC_METADATA_HEADER_SIZE = 4u,
    AUDIO_FLAC_METADATA_STREAMINFO = 0u,
    AUDIO_FLAC_METADATA_VORBIS_COMMENT = 4u,
    AUDIO_FLAC_METADATA_RESERVED = 127u
};

static uint16_t audio_ogg_flac_read_u16_be(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t audio_ogg_flac_read_u24_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 16u) |
           ((uint32_t)bytes[1] << 8u) | bytes[2];
}

static int audio_ogg_flac_is_frame(const uint8_t *bytes, size_t size)
{
    return bytes && size >= 5u && bytes[0] == 0xffu &&
           (bytes[1] & 0xfcu) == 0xf8u && (bytes[1] & 0x02u) == 0u;
}

static int audio_ogg_flac_parse_metadata_packet(const QwAudioOggPacket *packet,
                                                int first_metadata_packet)
{
    uint8_t type;
    uint32_t block_size;
    if (!packet || !packet->data ||
        packet->size < AUDIO_OGG_FLAC_METADATA_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    type = packet->data[0] & 0x7fu;
    block_size = audio_ogg_flac_read_u24_be(packet->data + 1u);
    if ((size_t)block_size !=
        packet->size - AUDIO_OGG_FLAC_METADATA_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (type == AUDIO_FLAC_METADATA_STREAMINFO ||
        type == AUDIO_FLAC_METADATA_RESERVED ||
        (first_metadata_packet && type != AUDIO_FLAC_METADATA_VORBIS_COMMENT)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

static int audio_ogg_flac_parse_frame_packet(
    const QwAudioFlacSource *stream,
    const QwAudioOggPacket *packet,
    uint8_t *blocking_strategy,
    QwAudioFlacFrame *frame)
{
    QwAudioFlacSource packet_source;
    int rc;
    if (!stream || !packet || !packet->data || !blocking_strategy || !frame ||
        !audio_ogg_flac_is_frame(packet->data, packet->size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_source = *stream;
    packet_source.data = packet->data;
    packet_source.size = packet->size;
    packet_source.first_frame_offset = 0u;
    packet_source.frame_data_end = packet->size;
    if (*blocking_strategy == 0xffu) {
        *blocking_strategy = packet->data[1] & 1u;
    }
    if ((packet->data[1] & 1u) != *blocking_strategy)
        return QW_STATUS_ERR_UNSUPPORTED;
    packet_source.blocking_strategy = *blocking_strategy;
    rc = qw_audio_flac_next_frame(&packet_source, 0u, frame);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    if (frame->size != packet->size || frame->next_position != packet->size)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}

static int audio_ogg_flac_validate_mapping_packet(
    const QwAudioOggPacket *packet,
    QwAudioFlacSource *flac,
    uint16_t *header_packet_count)
{
    uint32_t streaminfo_size;
    if (!packet || !packet->data || !flac || !header_packet_count)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet->size < 5u || packet->data[0] != 0x7fu ||
        memcmp(packet->data + 1u, "FLAC", 4u) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (packet->size != AUDIO_OGG_FLAC_MAPPING_PACKET_SIZE)
        return QW_STATUS_ERR_FORMAT;
    if (packet->data[5u] != AUDIO_OGG_FLAC_MAPPING_MAJOR ||
        packet->data[6u] != AUDIO_OGG_FLAC_MAPPING_MINOR) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (memcmp(packet->data + 9u, "fLaC", 4u) != 0 ||
        (packet->data[13u] & 0x7fu) != AUDIO_FLAC_METADATA_STREAMINFO)
        return QW_STATUS_ERR_FORMAT;
    streaminfo_size = audio_ogg_flac_read_u24_be(packet->data + 14u);
    if (streaminfo_size != QW_AUDIO_FLAC_STREAMINFO_SIZE ||
        AUDIO_OGG_FLAC_STREAMINFO_OFFSET + streaminfo_size != packet->size) {
        return QW_STATUS_ERR_FORMAT;
    }
    *header_packet_count = audio_ogg_flac_read_u16_be(packet->data + 7u);
    return qw_audio_flac_parse_streaminfo(
        packet->data + AUDIO_OGG_FLAC_STREAMINFO_OFFSET,
        streaminfo_size, flac);
}

static int audio_ogg_flac_parse_with_buffer(
    const uint8_t *bytes,
    size_t size,
    QwAudioOggFlacSource *source,
    QwAudioOggPacketBuffer *packet_buffer,
    QwConvertReport *report)
{
    QwAudioOggPacketCursor cursor;
    QwAudioOggPacketCursor first_audio_cursor;
    QwAudioOggPacketCursor scan_cursor;
    QwAudioOggPacket packet;
    uint8_t blocking_strategy = 0xffu;
    uint16_t header_packet_count;
    uint16_t metadata_packet_count = 0u;
    size_t last_header_page_offset;
    uint64_t decoded_samples = 0u;
    uint64_t previous_granule = 0u;
    uint64_t final_granule = UINT64_MAX;
    uint32_t last_frame_samples = 0u;
    int seen_comment = 0;
    int seen_eos = 0;
    int is_legacy_mapping = 0;
    int rc;
    (void)report;
    if (!bytes || !source || !packet_buffer)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || memcmp(bytes, "OggS", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;

    rc = qw_audio_ogg_packet_cursor_init(&cursor, bytes, size);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
    if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    if (packet.size >= 4u && memcmp(packet.data, "fLaC", 4u) == 0) {
        is_legacy_mapping = 1;
        rc = qw_audio_ogg_flac_legacy_parse_headers(
            bytes, size, &source->flac, packet_buffer, &first_audio_cursor);
        if (rc != QW_STATUS_OK) return rc;
    } else {
        if ((packet.page_flags & QW_AUDIO_OGG_FLAG_BOS) == 0u ||
            packet.page_segment_count != 1u || !packet.last_packet_on_page ||
            packet.granule_position != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = audio_ogg_flac_validate_mapping_packet(
            &packet, &source->flac, &header_packet_count);
        if (rc != QW_STATUS_OK) return rc;
        source->flac.data = bytes;
        source->flac.size = size;

        for (;;) {
            QwAudioOggPacketCursor before_packet = cursor;
            int first_metadata_packet = metadata_packet_count == 0u;
            if (header_packet_count &&
                metadata_packet_count >= header_packet_count) {
                first_audio_cursor = cursor;
                rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
                if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
                if (!audio_ogg_flac_is_frame(packet.data, packet.size))
                    return QW_STATUS_ERR_FORMAT;
                cursor = first_audio_cursor;
                break;
            }
            rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
            if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
            if (packet.granule_position != 0u) return QW_STATUS_ERR_FORMAT;
            if (audio_ogg_flac_is_frame(packet.data, packet.size)) {
                if (header_packet_count || !seen_comment)
                    return QW_STATUS_ERR_FORMAT;
                first_audio_cursor = before_packet;
                cursor = before_packet;
                break;
            }
            rc = audio_ogg_flac_parse_metadata_packet(&packet,
                                                      first_metadata_packet);
            if (rc != QW_STATUS_OK) return rc;
            if (first_metadata_packet) seen_comment = 1;
            ++metadata_packet_count;
            last_header_page_offset = packet.page_offset;
        }
    }

    scan_cursor = first_audio_cursor;
    if (!is_legacy_mapping) {
        QwAudioOggPacketCursor probe = first_audio_cursor;
        rc = qw_audio_ogg_packet_next(&probe, packet_buffer, &packet);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (packet.first_segment_index != 0u ||
            packet.first_page_offset == last_header_page_offset) {
            return QW_STATUS_ERR_FORMAT;
        }
    }

    for (;;) {
        QwAudioFlacFrame flac_frame;
        rc = qw_audio_ogg_packet_next(&scan_cursor, packet_buffer, &packet);
        if (rc == 0) break;
        if (rc < 0) return rc;
        rc = audio_ogg_flac_parse_frame_packet(
            &source->flac, &packet, &blocking_strategy, &flac_frame);
        if (rc != QW_STATUS_OK) return rc;
        if (flac_frame.sample_count > source->flac.max_block_size ||
            (decoded_samples &&
             last_frame_samples < source->flac.min_block_size) ||
            decoded_samples > UINT64_MAX - flac_frame.sample_count) {
            return QW_STATUS_ERR_FORMAT;
        }
        decoded_samples += flac_frame.sample_count;
        last_frame_samples = flac_frame.sample_count;
        if (packet.last_packet_on_page && !is_legacy_mapping) {
            uint64_t granule = packet.granule_position;
            if (granule == UINT64_MAX || granule < previous_granule ||
                granule > decoded_samples) {
                return QW_STATUS_ERR_FORMAT;
            }
            if ((packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                if (decoded_samples - granule > last_frame_samples)
                    return QW_STATUS_ERR_FORMAT;
                final_granule = granule;
                seen_eos = 1;
            } else if (granule != decoded_samples) {
                return QW_STATUS_ERR_FORMAT;
            }
            previous_granule = granule;
        } else if (packet.last_packet_on_page && is_legacy_mapping) {
            if ((packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                final_granule = source->flac.stream_sample_count
                    ? source->flac.stream_sample_count : decoded_samples;
                seen_eos = 1;
            }
        }
        if ((packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u &&
            !packet.last_packet_on_page) {
            continue;
        }
    }

    if ((!is_legacy_mapping && !seen_comment) || !seen_eos ||
        final_granule == UINT64_MAX ||
        !decoded_samples || final_granule == 0u ||
        (source->flac.stream_sample_count &&
         source->flac.stream_sample_count != final_granule)) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->flac.blocking_strategy = blocking_strategy;
    source->flac.sample_count = final_granule;
    source->packet_cursor = first_audio_cursor;
    return QW_STATUS_OK;
}

int qw_audio_ogg_flac_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioOggFlacSource *source,
                            QwConvertReport *report)
{
    QwAudioOggPacketBuffer packet_buffer = {0};
    int rc = audio_ogg_flac_parse_with_buffer(bytes, size, source,
                                              &packet_buffer, report);
    free(packet_buffer.data);
    return rc;
}

int qw_audio_ogg_flac_next_frame(QwAudioOggFlacSource *source,
                                 QwAudioOggPacketBuffer *packet_buffer,
                                 QwAudioOggFlacFrame *frame)
{
    QwAudioOggPacket packet;
    QwAudioFlacFrame flac_frame;
    uint8_t blocking_strategy;
    int rc;
    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    rc = qw_audio_ogg_packet_next(&source->packet_cursor, packet_buffer,
                                  &packet);
    if (rc <= 0) return rc;
    blocking_strategy = source->flac.blocking_strategy;
    rc = audio_ogg_flac_parse_frame_packet(&source->flac, &packet,
                                           &blocking_strategy, &flac_frame);
    if (rc != QW_STATUS_OK) return rc;
    frame->data = packet.data;
    frame->size = packet.size;
    frame->sample_count = flac_frame.sample_count;
    return 1;
}
