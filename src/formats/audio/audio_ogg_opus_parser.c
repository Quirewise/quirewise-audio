#include "audio_ogg_opus_parser.h"
#include "audio_ogg_container.h"
#include "audio_ogg_opus_head_parser.h"

#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

enum {
    AUDIO_OGG_OPUS_SAMPLE_RATE = 48000u,
    AUDIO_OGG_OPUS_MAX_PACKET_SAMPLES = 5760u
};

static int audio_ogg_opus_packet_samples(const uint8_t *packet,
                                         size_t packet_size,
                                         uint32_t *sample_count)
{
    uint32_t config;
    uint32_t frame_samples;
    uint32_t frame_count;
    uint32_t toc_count;
    uint32_t total_samples;
    static const uint16_t silk_frame_samples[4] = {480u, 960u, 1920u, 2880u};
    static const uint16_t celt_frame_samples[4] = {120u, 240u, 480u, 960u};
    if (!packet || !packet_size || !sample_count) return QW_STATUS_ERR_FORMAT;
    config = packet[0] >> 3u;
    toc_count = packet[0] & 3u;
    if (config < 12u) {
        frame_samples = silk_frame_samples[config & 3u];
    } else if (config < 16u) {
        frame_samples = (uint32_t)(480u << (config & 1u));
    } else {
        frame_samples = celt_frame_samples[config & 3u];
    }
    if (toc_count == 0u) {
        frame_count = 1u;
    } else if (toc_count < 3u) {
        frame_count = 2u;
    } else {
        if (packet_size < 2u) return QW_STATUS_ERR_FORMAT;
        frame_count = packet[1] & 0x3fu;
        if (!frame_count) return QW_STATUS_ERR_FORMAT;
    }
    if (frame_count > AUDIO_OGG_OPUS_MAX_PACKET_SAMPLES / frame_samples)
        return QW_STATUS_ERR_FORMAT;
    total_samples = frame_samples * frame_count;
    if (!total_samples || total_samples > AUDIO_OGG_OPUS_MAX_PACKET_SAMPLES)
        return QW_STATUS_ERR_FORMAT;
    *sample_count = total_samples;
    return QW_STATUS_OK;
}

static int audio_ogg_opus_parse_with_buffer(
    const uint8_t *bytes,
    size_t size,
    QwAudioOggOpusSource *source,
    QwAudioOggPacketBuffer *packet_buffer,
    QwConvertReport *report)
{
    QwAudioOggPacketCursor cursor;
    QwAudioOggPacketCursor first_audio_cursor;
    QwAudioOggPacket packet;
    size_t current_page_offset = SIZE_MAX;
    uint64_t decoded_samples = 0u;
    uint64_t final_granule = UINT64_MAX;
    uint64_t previous_audio_granule = 0u;
    size_t audio_packet_count = 0u;
    int page_has_audio = 0;
    int seen_head = 0;
    int seen_tags = 0;
    int seen_audio = 0;
    int seen_eos = 0;
    int rc;
    (void)report;
    if (!bytes || !source || !packet_buffer)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || memcmp(bytes, "OggS", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_ogg_packet_cursor_init(&cursor, bytes, size);
    if (rc != QW_STATUS_OK) return rc;

    for (;;) {
        QwAudioOggPacketCursor before_packet = cursor;
        uint64_t expected_granule;
        uint32_t packet_samples;
        rc = qw_audio_ogg_packet_next(&cursor, packet_buffer, &packet);
        if (rc == 0) break;
        if (rc < 0) return rc;
        if (current_page_offset != packet.page_offset) {
            current_page_offset = packet.page_offset;
            page_has_audio = 0;
        }

        if (!seen_head) {
            if (packet.size < 8u ||
                memcmp(packet.data, "OpusHead", 8u) != 0) {
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            rc = qw_audio_ogg_opus_head_parse(packet.data, packet.size,
                                              source);
            if (rc != QW_STATUS_OK) return rc;
            seen_head = 1;
        } else if (!seen_tags) {
            rc = qw_audio_ogg_opus_tags_validate(packet.data, packet.size);
            if (rc != QW_STATUS_OK) return rc;
            seen_tags = 1;
        } else {
            rc = audio_ogg_opus_packet_samples(packet.data, packet.size,
                                               &packet_samples);
            if (rc != QW_STATUS_OK) return rc;
            if ((uint64_t)packet_samples > UINT64_MAX - decoded_samples)
                return QW_STATUS_ERR_FORMAT;
            decoded_samples += packet_samples;
            ++audio_packet_count;
            page_has_audio = 1;
            if (!seen_audio) {
                first_audio_cursor = before_packet;
                source->first_audio_page_offset = packet.first_page_offset;
                source->first_audio_segment_index =
                    packet.first_segment_index;
                seen_audio = 1;
            }
        }

        if (!packet.last_packet_on_page) continue;
        if (page_has_audio) {
            if (decoded_samples > UINT64_MAX - source->pre_skip)
                return QW_STATUS_ERR_FORMAT;
            expected_granule = decoded_samples + source->pre_skip;
            if (packet.granule_position == UINT64_MAX ||
                packet.granule_position > expected_granule ||
                packet.granule_position < previous_audio_granule) {
                return QW_STATUS_ERR_FORMAT;
            }
            if ((packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
                if (packet.granule_position < source->pre_skip)
                    return QW_STATUS_ERR_FORMAT;
                final_granule = packet.granule_position;
                seen_eos = 1;
            } else if (packet.granule_position != expected_granule) {
                return QW_STATUS_ERR_FORMAT;
            }
            previous_audio_granule = packet.granule_position;
        } else if (packet.granule_position != 0u ||
                   (packet.page_flags & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (seen_eos) break;
    }

    if (!seen_head || !seen_tags || !seen_audio || !audio_packet_count ||
        !seen_eos || final_granule == UINT64_MAX ||
        final_granule < source->pre_skip || cursor.page_position != size) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->data = bytes;
    source->size = size;
    source->frame_data_end = size;
    source->sample_count = final_granule - source->pre_skip;
    source->serial_number = cursor.serial_number;
    source->sample_rate = AUDIO_OGG_OPUS_SAMPLE_RATE;
    source->packet_cursor = first_audio_cursor;
    source->has_metadata = 1;
    return source->sample_count ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_ogg_opus_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioOggOpusSource *source,
                            QwConvertReport *report)
{
    QwAudioOggPacketBuffer packet_buffer = {0};
    int rc;
    if (!source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = audio_ogg_opus_parse_with_buffer(bytes, size, source,
                                          &packet_buffer, report);
    free(packet_buffer.data);
    if (rc != QW_STATUS_OK) qw_audio_ogg_opus_source_dispose(source);
    return rc;
}

void qw_audio_ogg_opus_source_dispose(QwAudioOggOpusSource *source)
{
    if (!source) return;
    free(source->demixing_matrix);
    source->demixing_matrix = NULL;
    source->demixing_matrix_stride = 0;
}

int qw_audio_ogg_opus_next_frame(QwAudioOggOpusSource *source,
                                 QwAudioOggPacketBuffer *packet_buffer,
                                 QwAudioOggOpusFrame *frame)
{
    QwAudioOggPacket packet;
    uint32_t sample_count;
    int rc;
    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    rc = qw_audio_ogg_packet_next(&source->packet_cursor, packet_buffer,
                                  &packet);
    if (rc <= 0) return rc;
    rc = audio_ogg_opus_packet_samples(packet.data, packet.size,
                                       &sample_count);
    if (rc != QW_STATUS_OK) return rc;
    frame->data = packet.data;
    frame->size = packet.size;
    frame->sample_count = sample_count;
    return 1;
}
