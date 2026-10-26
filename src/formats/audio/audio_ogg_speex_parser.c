#include "audio_ogg_speex_parser.h"

#include "../../core/byte_utils.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

enum {
    AUDIO_OGG_SPEEX_HEADER_PREFIX_SIZE = 8u,
    AUDIO_OGG_SPEEX_SAMPLE_RATE_OFFSET = 36u,
    AUDIO_OGG_SPEEX_MODE_OFFSET = 40u,
    AUDIO_OGG_SPEEX_BITSTREAM_VERSION_OFFSET = 44u,
    AUDIO_OGG_SPEEX_CHANNELS_OFFSET = 48u,
    AUDIO_OGG_SPEEX_FRAME_SIZE_OFFSET = 56u,
    AUDIO_OGG_SPEEX_VBR_OFFSET = 60u,
    AUDIO_OGG_SPEEX_FRAMES_PER_PACKET_OFFSET = 64u,
    AUDIO_OGG_SPEEX_EXTRA_HEADERS_OFFSET = 68u,
    AUDIO_OGG_SPEEX_RESERVED1_OFFSET = 72u,
    AUDIO_OGG_SPEEX_RESERVED2_OFFSET = 76u
};

static int audio_ogg_speex_validate_comment(const QwAudioOggPacket *packet)
{
    size_t position;
    uint32_t vendor_size;
    uint32_t comment_count;
    uint32_t i;

    if (!packet || !packet->data || packet->size < 8u)
        return QW_STATUS_ERR_FORMAT;
    vendor_size = qw_read_u32_le(packet->data);
    if ((size_t)vendor_size > packet->size - 8u)
        return QW_STATUS_ERR_FORMAT;
    position = 4u + (size_t)vendor_size;
    comment_count = qw_read_u32_le(packet->data + position);
    position += 4u;

    if ((uint64_t)comment_count >
        (uint64_t)(packet->size - position) / 4u) {
        return QW_STATUS_ERR_FORMAT;
    }
    for (i = 0u; i < comment_count; ++i) {
        uint32_t comment_size;
        if (packet->size - position < 4u) return QW_STATUS_ERR_FORMAT;
        comment_size = qw_read_u32_le(packet->data + position);
        position += 4u;
        if ((size_t)comment_size > packet->size - position)
            return QW_STATUS_ERR_FORMAT;
        position += (size_t)comment_size;
    }
    return position == packet->size ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int audio_ogg_speex_parse_header(const QwAudioOggPacket *packet,
                                        QwAudioOggSpeexSource *source,
                                        uint32_t *extra_headers)
{
    uint32_t mode;
    uint32_t channels;
    uint32_t frame_size;
    uint32_t frames_per_packet;
    uint32_t rate;
    uint32_t vbr;
    uint32_t extra;

    if (!packet || !packet->data || !source || !extra_headers)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet->size < AUDIO_OGG_SPEEX_HEADER_PREFIX_SIZE ||
        memcmp(packet->data, "Speex   ", AUDIO_OGG_SPEEX_HEADER_PREFIX_SIZE) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (packet->size != QW_AUDIO_OGG_SPEEX_HEADER_SIZE ||
        qw_read_u32_le(packet->data + 32u) !=
            QW_AUDIO_OGG_SPEEX_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }

    rate = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_SAMPLE_RATE_OFFSET);
    mode = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_MODE_OFFSET);
    channels = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_CHANNELS_OFFSET);
    frame_size = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_FRAME_SIZE_OFFSET);
    vbr = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_VBR_OFFSET);
    frames_per_packet =
        qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_FRAMES_PER_PACKET_OFFSET);
    extra = qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_EXTRA_HEADERS_OFFSET);

    if (mode > 2u || rate == 0u || rate > INT_MAX ||
        qw_read_u32_le(packet->data +
                       AUDIO_OGG_SPEEX_BITSTREAM_VERSION_OFFSET) != 4u ||
        (channels != 1u && channels != 2u) ||
        frame_size != (160u << mode) || vbr > 1u ||
        frames_per_packet == 0u || frames_per_packet > 64u ||
        extra > INT_MAX ||
        qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_RESERVED1_OFFSET) != 0u ||
        qw_read_u32_le(packet->data + AUDIO_OGG_SPEEX_RESERVED2_OFFSET) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    source->sample_rate = rate;
    source->channels = (uint8_t)channels;
    source->samples_per_packet = frame_size * frames_per_packet;
    memcpy(source->speex_header, packet->data, sizeof(source->speex_header));
    *extra_headers = extra;
    return QW_STATUS_OK;
}

static int audio_ogg_speex_validate_intermediate_pages(
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

static int audio_ogg_speex_parse_with_buffer(
    const uint8_t *bytes,
    size_t size,
    QwAudioOggSpeexSource *source,
    QwAudioOggPacketBuffer *packet_buffer)
{
    QwAudioOggPacketCursor cursor;
    QwAudioOggPacketCursor first_audio_cursor = {0};
    QwAudioOggPacket packet;
    size_t current_page_offset = SIZE_MAX;
    uint64_t previous_granule = 0u;
    uint64_t final_granule = 0u;
    uint64_t total_audio_samples = 0u;
    uint64_t page_audio_samples = 0u;
    uint32_t extra_headers = 0u;
    int have_previous_granule = 0;
    int page_has_audio = 0;
    int seen_header = 0;
    int seen_comment = 0;
    int seen_audio = 0;
    int seen_eos = 0;
    int rc;

    if (!bytes || !source || !packet_buffer)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < AUDIO_OGG_SPEEX_HEADER_PREFIX_SIZE ||
        memcmp(bytes, "OggS", 4u) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
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
        rc = audio_ogg_speex_validate_intermediate_pages(bytes, size, &packet);
        if (rc != QW_STATUS_OK) goto done;
        if (current_page_offset != packet.page_offset) {
            current_page_offset = packet.page_offset;
            page_has_audio = 0;
            page_audio_samples = 0u;
        }

        if (!seen_header) {
            rc = audio_ogg_speex_parse_header(&packet, source,
                                              &extra_headers);
            if (rc != QW_STATUS_OK) goto done;
            if (packet.first_page_offset != 0u ||
                packet.first_segment_index != 0u ||
                (packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            seen_header = 1;
        } else if (!seen_comment) {
            rc = audio_ogg_speex_validate_comment(&packet);
            if (rc != QW_STATUS_OK) goto done;
            seen_comment = 1;
        } else if (extra_headers) {
            if (!packet.data || !packet.size) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            --extra_headers;
        } else {
            if (!packet.data || !packet.size) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            if (!seen_audio) {
                first_audio_cursor = before_packet;
                seen_audio = 1;
            }
            if ((uint64_t)source->samples_per_packet >
                    UINT64_MAX - total_audio_samples ||
                (uint64_t)source->samples_per_packet >
                    UINT64_MAX - page_audio_samples ||
                (uint64_t)packet.size > UINT64_MAX - source->audio_bytes) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            total_audio_samples += source->samples_per_packet;
            page_audio_samples += source->samples_per_packet;
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
            uint64_t granule = packet.granule_position;
            uint64_t page_start_samples = total_audio_samples -
                                         page_audio_samples;
            int is_eos =
                (packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u;
            if (granule == UINT64_MAX || granule > (uint64_t)INT64_MAX ||
                (have_previous_granule && granule < previous_granule) ||
                (is_eos
                    ? (granule < page_start_samples ||
                       granule > total_audio_samples)
                    : granule != total_audio_samples)) {
                rc = QW_STATUS_ERR_FORMAT;
                goto done;
            }
            previous_granule = granule;
            have_previous_granule = 1;
            if (is_eos) {
                final_granule = granule;
                seen_eos = 1;
            }
        }
    }

    if (!seen_header || !seen_comment || extra_headers || !seen_audio ||
        !seen_eos || !final_granule || cursor.page_position != size) {
        rc = QW_STATUS_ERR_FORMAT;
        goto done;
    }
    source->sample_count = final_granule;
    source->packet_cursor = first_audio_cursor;
    rc = QW_STATUS_OK;

done:
    return rc;
}

int qw_audio_ogg_speex_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioOggSpeexSource *source,
                             QwConvertReport *report)
{
    QwAudioOggPacketBuffer packet_buffer = {0};
    int rc;
    (void)report;
    rc = audio_ogg_speex_parse_with_buffer(bytes, size, source,
                                           &packet_buffer);
    free(packet_buffer.data);
    return rc;
}

int qw_audio_ogg_speex_next_frame(QwAudioOggSpeexSource *source,
                                  QwAudioOggPacketBuffer *packet_buffer,
                                  QwAudioOggSpeexFrame *frame)
{
    QwAudioOggPacket packet;
    int rc;

    if (!source || !packet_buffer || !frame)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    rc = qw_audio_ogg_packet_next(&source->packet_cursor, packet_buffer,
                                  &packet);
    if (rc <= 0) return rc;
    if (!packet.data || !packet.size)
        return QW_STATUS_ERR_FORMAT;
    frame->data = packet.data;
    frame->size = packet.size;
    frame->sample_count = source->samples_per_packet;
    return 1;
}
