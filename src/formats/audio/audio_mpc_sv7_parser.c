#include "audio_mpc_parser_internal.h"
#include "audio_mpc_sv7_parser_internal.h"

#include "audio_trailing_tags.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_mpc_sv7_next_packet(QwAudioMpcSource *source,
                                 QwAudioMpcPacket *packet)
{
    size_t position;
    size_t packet_size;
    size_t packet_end;
    size_t next_position;
    size_t frame_data_end;
    uint32_t first_word;
    uint32_t second_word = 0u;
    uint32_t frame_bits;
    uint32_t next_bit_position;
    unsigned curbits;
    unsigned next_curbits;
    uint32_t last_frame_flag;

    if (source->frame_count && source->current_frame >= source->frame_count)
        return 0;
    position = source->frame_position;
    frame_data_end = source->frame_data_end;
    curbits = source->frame_bit_position;
    if (frame_data_end > source->size || position > frame_data_end ||
        frame_data_end - position < 4u)
        return QW_STATUS_ERR_FORMAT;
    first_word = qw_audio_mpc_read_u32_le(source->data + position);
    if (curbits <= 12u) {
        frame_bits = (first_word >> (12u - curbits)) & UINT32_C(0xfffff);
    } else {
        if (frame_data_end - position < 8u) return QW_STATUS_ERR_FORMAT;
        second_word = qw_audio_mpc_read_u32_le(source->data + position + 4u);
        frame_bits = ((first_word << (curbits - 12u)) |
                      (second_word >> (44u - curbits))) &
                     UINT32_C(0xfffff);
    }
    next_curbits = curbits + 20u;
    packet_size =
        (size_t)((frame_bits + next_curbits + 31u) & ~UINT32_C(31)) >> 3u;
    if (packet_size > frame_data_end - position)
        return QW_STATUS_ERR_FORMAT;
    next_bit_position = (next_curbits + frame_bits) & 31u;
    packet_end = position + packet_size;
    next_position = packet_end;
    if (next_bit_position && packet_end < frame_data_end)
        next_position -= 4u;
    if (packet_end < frame_data_end && next_position <= position)
        return QW_STATUS_ERR_FORMAT;

    packet->data = source->data + position;
    packet->size = packet_size;
    packet->decoder_header[0] = (uint8_t)next_curbits;
    last_frame_flag = source->frame_count &&
        source->current_frame + 1u > source->frame_count;
    packet->decoder_header[1] = (uint8_t)last_frame_flag;
    packet->decoder_header[2] = 0u;
    packet->decoder_header[3] = 0u;
    packet->decoder_header_size = 4u;

    source->frame_position = next_position;
    source->frame_bit_position = (uint8_t)next_bit_position;
    ++source->current_frame;
    return 1;
}

static int mpc_scan_sv7_frames(const QwAudioMpcSource *source,
                               uint32_t declared_frame_count,
                               uint32_t *actual_frame_count,
                               uint64_t *packet_bytes)
{
    QwAudioMpcSource scan;
    uint32_t scanned_frames = 0u;

    if (!source || !actual_frame_count || !packet_bytes)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    scan = *source;
    scan.frame_count = declared_frame_count;
    for (;;) {
        QwAudioMpcPacket packet;
        size_t previous_position = scan.frame_position;
        int rc = qw_audio_mpc_sv7_next_packet(&scan, &packet);

        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (scanned_frames == UINT32_MAX ||
            scan.audio_packet_bytes > UINT64_MAX - (uint64_t)packet.size) {
            return QW_STATUS_ERR_FORMAT;
        }
        ++scanned_frames;
        scan.audio_packet_bytes += (uint64_t)packet.size;

        if (declared_frame_count) {
            if (scanned_frames == declared_frame_count) break;
        } else if (scan.frame_position == scan.frame_data_end) {
            break;
        }
        if (scan.frame_position <= previous_position)
            return QW_STATUS_ERR_FORMAT;
    }
    if (!scanned_frames ||
        (!declared_frame_count &&
         scan.frame_position != scan.frame_data_end)) {
        return QW_STATUS_ERR_FORMAT;
    }

    *actual_frame_count = scanned_frames;
    *packet_bytes = scan.audio_packet_bytes;
    return QW_STATUS_OK;
}

int qw_audio_mpc_sv7_parse(const uint8_t *data,
                           size_t size,
                           QwAudioMpcSource *source)
{
    static const uint32_t sample_rates[4] = {
        44100u, 48000u, 37800u, 32000u
    };
    size_t frame_data_end;
    uint32_t frame_count;
    uint32_t actual_frame_count;
    uint64_t packet_bytes;
    int rc;

    if (size < 4u || memcmp(data, "MP+", 3u) != 0 ||
        (data[3] != 0x07u && data[3] != 0x17u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (size < 24u) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_trailing_tags_audio_end(data, size, &frame_data_end);
    if (rc != QW_STATUS_OK) return rc;
    if (frame_data_end < 24u) return QW_STATUS_ERR_FORMAT;
    frame_count = qw_audio_mpc_read_u32_le(data + 4u);
    if (frame_data_end - 24u > UINT64_MAX / 8u ||
        (frame_count &&
         (uint64_t)frame_count >
             ((uint64_t)(frame_data_end - 24u) * 8u) / 20u)) {
        return QW_STATUS_ERR_FORMAT;
    }

    source->data = data;
    source->size = size;
    source->frame_data_end = frame_data_end;
    source->is_sv8 = 0u;
    source->frame_count = frame_count;
    source->frame_samples = 1152u;
    source->frame_position = 24u;
    source->frame_bit_position = 8u;
    source->channels = 2u;
    source->decoder_extradata_size = 16u;
    memcpy(source->decoder_extradata, data + 8u,
           source->decoder_extradata_size);
    source->sample_rate = sample_rates[source->decoder_extradata[2] & 0x03u];
    rc = mpc_scan_sv7_frames(source, frame_count, &actual_frame_count,
                             &packet_bytes);
    if (rc != QW_STATUS_OK) return rc;
    source->frame_count = actual_frame_count;
    source->sample_count = (uint64_t)actual_frame_count * UINT64_C(1152);
    source->audio_packet_bytes = packet_bytes;
    return QW_STATUS_OK;
}
