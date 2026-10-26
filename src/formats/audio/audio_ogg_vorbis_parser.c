#include "audio_ogg_vorbis_parser.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    AUDIO_OGG_VORBIS_IDENTIFICATION_PACKET_SIZE = 30u,
    AUDIO_OGG_VORBIS_HEADER_PREFIX_SIZE = 7u
};

static int audio_ogg_vorbis_has_header_signature(const QwAudioOggPacket *packet,
                                                uint8_t packet_type)
{
    return packet && packet->data &&
           packet->size >= AUDIO_OGG_VORBIS_HEADER_PREFIX_SIZE &&
           packet->data[0] == packet_type &&
           memcmp(packet->data + 1u, "vorbis", 6u) == 0;
}

static int audio_ogg_vorbis_parse_identification(
    const QwAudioOggPacket *packet,
    QwAudioOggVorbisSource *source)
{
    uint32_t version;
    uint32_t sample_rate;
    uint8_t block_sizes;
    unsigned small_block_exponent;
    unsigned large_block_exponent;

    if (!audio_ogg_vorbis_has_header_signature(packet, 1u))
        return QW_STATUS_ERR_UNSUPPORTED;
    if (packet->size != AUDIO_OGG_VORBIS_IDENTIFICATION_PACKET_SIZE)
        return QW_STATUS_ERR_FORMAT;

    version = qw_read_u32_le(packet->data + 7u);
    source->channels = packet->data[11u];
    sample_rate = qw_read_u32_le(packet->data + 12u);
    block_sizes = packet->data[28u];
    small_block_exponent = block_sizes & 0x0fu;
    large_block_exponent = block_sizes >> 4u;

    if (version != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    if (!source->channels || !sample_rate || packet->data[29u] != 1u ||
        small_block_exponent < 6u || large_block_exponent > 13u ||
        small_block_exponent > large_block_exponent) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (sample_rate > INT_MAX) return QW_STATUS_ERR_UNSUPPORTED;
    source->sample_rate = sample_rate;
    return QW_STATUS_OK;
}

static int audio_ogg_vorbis_validate_comment(const QwAudioOggPacket *packet)
{
    size_t position;
    uint32_t vendor_size;
    uint32_t comment_count;
    uint32_t i;

    if (!audio_ogg_vorbis_has_header_signature(packet, 3u) ||
        packet->size < 16u) {
        return QW_STATUS_ERR_FORMAT;
    }

    vendor_size = qw_read_u32_le(packet->data + 7u);
    position = 11u;
    if ((size_t)vendor_size > packet->size - position)
        return QW_STATUS_ERR_FORMAT;
    position += (size_t)vendor_size;
    if (packet->size - position < 5u) return QW_STATUS_ERR_FORMAT;

    comment_count = qw_read_u32_le(packet->data + position);
    position += 4u;
    if ((uint64_t)comment_count >
        (uint64_t)(packet->size - position - 1u) / 4u) {
        return QW_STATUS_ERR_FORMAT;
    }
    for (i = 0u; i < comment_count; ++i) {
        uint32_t comment_size;
        if (packet->size - position < 5u) return QW_STATUS_ERR_FORMAT;
        comment_size = qw_read_u32_le(packet->data + position);
        position += 4u;
        if ((size_t)comment_size > packet->size - position - 1u)
            return QW_STATUS_ERR_FORMAT;
        position += (size_t)comment_size;
    }
    return packet->size - position == 1u && packet->data[position] == 1u
        ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int audio_ogg_vorbis_copy_packet(const QwAudioOggPacket *packet,
                                       uint8_t **copy)
{
    uint8_t *data;
    if (!packet || !packet->data || !packet->size || !copy)
        return QW_STATUS_ERR_FORMAT;
    data = (uint8_t *)malloc(packet->size);
    if (!data) return QW_STATUS_ERR_OUT_OF_MEMORY;
    memcpy(data, packet->data, packet->size);
    *copy = data;
    return QW_STATUS_OK;
}

static size_t audio_ogg_vorbis_xiph_lacing_size(size_t packet_size)
{
    return packet_size / 255u + 1u;
}

static void audio_ogg_vorbis_write_xiph_lacing(uint8_t *output,
                                               size_t *position,
                                               size_t packet_size)
{
    while (packet_size >= 255u) {
        output[(*position)++] = 255u;
        packet_size -= 255u;
    }
    output[(*position)++] = (uint8_t)packet_size;
}

static int audio_ogg_vorbis_build_decoder_extradata(
    const uint8_t *identification,
    size_t identification_size,
    const uint8_t *comment,
    size_t comment_size,
    const uint8_t *setup,
    size_t setup_size,
    uint8_t **extradata,
    size_t *extradata_size)
{
    size_t total_size = 1u;
    size_t position = 0u;
    uint8_t *output;

    if (!identification || !comment || !setup || !extradata ||
        !extradata_size || !identification_size || !comment_size ||
        !setup_size) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }

    if (audio_ogg_vorbis_xiph_lacing_size(identification_size) >
            SIZE_MAX - total_size ||
        audio_ogg_vorbis_xiph_lacing_size(comment_size) >
            SIZE_MAX - total_size -
                audio_ogg_vorbis_xiph_lacing_size(identification_size)) {
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    }
    total_size += audio_ogg_vorbis_xiph_lacing_size(identification_size);
    total_size += audio_ogg_vorbis_xiph_lacing_size(comment_size);
    if (identification_size > SIZE_MAX - total_size)
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    total_size += identification_size;
    if (comment_size > SIZE_MAX - total_size)
        return QW_STATUS_ERR_OUT_OF_MEMORY;
    total_size += comment_size;
    if (setup_size > SIZE_MAX - total_size || total_size + setup_size > INT_MAX)
        return QW_STATUS_ERR_UNSUPPORTED;
    total_size += setup_size;

    output = (uint8_t *)malloc(total_size);
    if (!output) return QW_STATUS_ERR_OUT_OF_MEMORY;
    output[position++] = 2u;
    audio_ogg_vorbis_write_xiph_lacing(output, &position, identification_size);
    audio_ogg_vorbis_write_xiph_lacing(output, &position, comment_size);
    memcpy(output + position, identification, identification_size);
    position += identification_size;
    memcpy(output + position, comment, comment_size);
    position += comment_size;
    memcpy(output + position, setup, setup_size);

    *extradata = output;
    *extradata_size = total_size;
    return QW_STATUS_OK;
}

static int audio_ogg_vorbis_validate_intermediate_pages(
    const uint8_t *bytes,
    size_t size,
    const QwAudioOggPacket *packet)
{
    QwAudioOggPage page;
    size_t page_offset;
    int rc;

    if (!bytes || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!packet->crossed_pages) return QW_STATUS_OK;
    rc = qw_audio_ogg_page_parse(bytes, size, packet->first_page_offset,
                                 &page);
    if (rc != QW_STATUS_OK) return rc;
    page_offset = page.next_offset;
    while (page_offset < packet->page_offset) {
        rc = qw_audio_ogg_page_parse(bytes, size, page_offset, &page);
        if (rc != QW_STATUS_OK) return rc;
        if (page.granule_position != UINT64_MAX ||
            page.next_offset <= page_offset) {
            return QW_STATUS_ERR_FORMAT;
        }
        page_offset = page.next_offset;
    }
    return page_offset == packet->page_offset ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int audio_ogg_vorbis_granule_as_signed(uint64_t raw,
                                               int64_t *granule)
{
    uint64_t magnitude;
    if (!granule || raw == UINT64_MAX) return 0;
    if (raw <= (uint64_t)INT64_MAX) {
        *granule = (int64_t)raw;
        return 1;
    }
    magnitude = (~raw) + 1u;
    if (magnitude == (UINT64_C(1) << 63u)) {
        *granule = INT64_MIN;
    } else {
        *granule = -(int64_t)magnitude;
    }
    return 1;
}

static int audio_ogg_vorbis_parse_with_buffer(
    const uint8_t *bytes,
    size_t size,
    QwAudioOggVorbisSource *source,
    QwAudioOggPacketBuffer *packet_buffer)
{
    QwAudioOggPacketCursor cursor;
    QwAudioOggPacketCursor first_audio_cursor = {0};
    QwAudioOggPacket packet;
    uint8_t *identification = NULL;
    uint8_t *comment = NULL;
    uint8_t *setup = NULL;
    size_t identification_size = 0u;
    size_t comment_size = 0u;
    size_t setup_size = 0u;
    size_t setup_page_offset = SIZE_MAX;
    size_t current_page_offset = SIZE_MAX;
    int64_t previous_audio_granule = 0;
    int64_t final_granule = 0;
    int have_previous_audio_granule = 0;
    int page_has_audio = 0;
    int seen_identification = 0;
    int seen_comment = 0;
    int seen_setup = 0;
    int seen_audio = 0;
    int seen_eos = 0;
    int rc;

    if (!bytes || !source || !packet_buffer)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || memcmp(bytes, "OggS", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_ogg_packet_cursor_init(&cursor, bytes, size);
    if (rc != QW_STATUS_OK) return rc;

    for (;;) {
        QwAudioOggPacketCursor before_packet = cursor;
        rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
        if (rc == 0) break;
        if (rc < 0) {
            if (seen_eos && rc == QW_STATUS_ERR_FORMAT)
                rc = QW_STATUS_ERR_UNSUPPORTED;
            goto done;
        }
        rc = audio_ogg_vorbis_validate_intermediate_pages(bytes, size,
                                                         &packet);
        if (rc != QW_STATUS_OK) goto done;
        if (current_page_offset != packet.page_offset) {
            current_page_offset = packet.page_offset;
            page_has_audio = 0;
        }

        if (!seen_identification) {
            if (!audio_ogg_vorbis_has_header_signature(&packet, 1u)) {
                rc = QW_STATUS_ERR_UNSUPPORTED;
                goto done;
            }
            rc = audio_ogg_vorbis_parse_identification(&packet, source);
            if (rc != QW_STATUS_OK) goto done;
            if (packet.first_page_offset != 0u ||
                packet.page_offset != 0u || packet.crossed_pages ||
                packet.first_segment_index != 0u ||
                packet.page_segment_count != 1u ||
                !packet.last_packet_on_page ||
                packet.page_flags != QW_AUDIO_OGG_FLAG_BOS ||
                packet.granule_position != 0u) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            rc = audio_ogg_vorbis_copy_packet(&packet, &identification);
            if (rc != QW_STATUS_OK) goto done;
            identification_size = packet.size;
            seen_identification = 1;
        } else if (!seen_comment) {
            rc = audio_ogg_vorbis_validate_comment(&packet);
            if (rc != QW_STATUS_OK) goto done;
            rc = audio_ogg_vorbis_copy_packet(&packet, &comment);
            if (rc != QW_STATUS_OK) goto done;
            comment_size = packet.size;
            seen_comment = 1;
        } else if (!seen_setup) {
            if (!audio_ogg_vorbis_has_header_signature(&packet, 5u)) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            rc = audio_ogg_vorbis_copy_packet(&packet, &setup);
            if (rc != QW_STATUS_OK) goto done;
            setup_size = packet.size;
            setup_page_offset = packet.page_offset;
            if (!packet.last_packet_on_page) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            seen_setup = 1;
        } else {
            if (!packet.data || !packet.size || (packet.data[0] & 1u) != 0u) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            if (!seen_audio) {
                if (packet.crossed_pages ||
                    packet.first_page_offset != packet.page_offset ||
                    packet.page_offset == setup_page_offset ||
                    packet.first_segment_index != 0u) {
                    rc = QW_STATUS_ERR_FORMAT;
                    goto done;
                }
                first_audio_cursor = before_packet;
                seen_audio = 1;
            }
            if ((uint64_t)packet.size > UINT64_MAX - source->audio_bytes) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            source->audio_bytes += (uint64_t)packet.size;
            page_has_audio = 1;
        }

        if (!packet.last_packet_on_page) continue;
        if (!page_has_audio) {
            if (packet.granule_position != 0u ||
                (packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
        } else {
            int64_t page_granule;
            if (!audio_ogg_vorbis_granule_as_signed(
                    packet.granule_position, &page_granule) ||
                (have_previous_audio_granule &&
                 page_granule < previous_audio_granule)) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            if ((packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                final_granule = page_granule;
                seen_eos = 1;
            }
            previous_audio_granule = page_granule;
            have_previous_audio_granule = 1;
        }
    }

    if (!seen_identification || !seen_comment || !seen_setup || !seen_audio ||
        !seen_eos || final_granule <= 0 ||
        cursor.page_position != size) {
        rc = QW_STATUS_ERR_FORMAT;
        goto done;
    }
    rc = audio_ogg_vorbis_build_decoder_extradata(
        identification, identification_size, comment, comment_size,
        setup, setup_size, &source->decoder_extradata,
        &source->decoder_extradata_size);
    if (rc != QW_STATUS_OK) goto done;
    source->sample_count = (uint64_t)final_granule;
    source->packet_cursor = first_audio_cursor;
    rc = QW_STATUS_OK;

done:
    free(identification);
    free(comment);
    free(setup);
    if (rc != QW_STATUS_OK) qw_audio_ogg_vorbis_source_dispose(source);
    return rc;
}

int qw_audio_ogg_vorbis_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioOggVorbisSource *source,
                              QwConvertReport *report)
{
    QwAudioOggPacketBuffer packet_buffer = {0};
    int rc;
    (void)report;
    rc = audio_ogg_vorbis_parse_with_buffer(bytes, size, source,
                                            &packet_buffer);
    free(packet_buffer.data);
    return rc;
}

int qw_audio_ogg_vorbis_next_frame(QwAudioOggVorbisSource *source,
                                   QwAudioOggPacketBuffer *packet_buffer,
                                   QwAudioOggVorbisFrame *frame)
{
    QwAudioOggPacket packet;
    int rc;
    if (!source || !packet_buffer || !frame)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    rc = qw_audio_ogg_packet_next(&source->packet_cursor, packet_buffer,
                                  &packet);
    if (rc <= 0) return rc;
    if (!packet.data || !packet.size || (packet.data[0] & 1u) != 0u)
        return QW_STATUS_ERR_FORMAT;
    frame->data = packet.data;
    frame->size = packet.size;
    return 1;
}

void qw_audio_ogg_vorbis_source_dispose(QwAudioOggVorbisSource *source)
{
    if (!source) return;
    free(source->decoder_extradata);
    source->decoder_extradata = NULL;
    source->decoder_extradata_size = 0u;
}
