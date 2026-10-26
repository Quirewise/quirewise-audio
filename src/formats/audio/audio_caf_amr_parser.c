#include "audio_caf_amr_parser.h"

#include "../../core/byte_utils.h"
#include "audio_amr_parser.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "audio_caf_varint.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    CAF_AMR_DESCRIPTION_SIZE = 32u,
    CAF_AMR_SAMPLE_RATE = 8000u,
    CAF_AMR_CHANNELS = 1u,
    CAF_AMR_FRAMES_PER_PACKET = 160u
};

static int caf_amr_parse_description(const uint8_t *bytes,
                                     size_t size)
{
    uint64_t sample_rate_bits;
    double sample_rate;

    if (!bytes) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_AMR_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "samr", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    if (sample_rate != (double)CAF_AMR_SAMPLE_RATE ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        qw_read_u32_be(bytes + 16u) != 0u ||
        qw_read_u32_be(bytes + 20u) != CAF_AMR_FRAMES_PER_PACKET ||
        qw_read_u32_be(bytes + 24u) != CAF_AMR_CHANNELS ||
        qw_read_u32_be(bytes + 28u) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

static int caf_amr_packet_validate(const uint8_t *data, size_t size)
{
    QwAudioAmrSource amr = {0};
    QwAudioAmrFrame frame;
    int rc;

    if (!data || !size) return QW_STATUS_ERR_FORMAT;
    amr.data = data;
    amr.size = size;
    amr.frame_data_end = size;
    amr.sample_rate = CAF_AMR_SAMPLE_RATE;
    amr.channels = CAF_AMR_CHANNELS;
    rc = qw_audio_amr_next_frame(&amr, 0u, &frame);
    if (rc != 1 || frame.size != size || frame.next_position != size ||
        frame.sample_count != CAF_AMR_FRAMES_PER_PACKET) {
        return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_caf_amr_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafAmrSource *source,
                           QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t packet_count;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    uint64_t i;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_amr_parse_description(chunks.description,
                                   chunks.description_size);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.packet_table ||
        chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE ||
        !chunks.audio_data || !chunks.audio_data_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_caf_packet_table_parse_header(
        chunks.packet_table, chunks.packet_table_size, &packet_table);
    if (rc != QW_STATUS_OK) return rc;
    packet_count = packet_table.packet_count;
    if (!packet_count || packet_count > (uint64_t)SIZE_MAX)
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_caf_packet_table_validate_frames(
        &packet_table, packet_count, CAF_AMR_FRAMES_PER_PACKET);
    if (rc != QW_STATUS_OK) return rc;

    for (i = 0u; i < packet_count; ++i) {
        uint64_t encoded_size;
        size_t packet_size;

        rc = qw_audio_caf_varint_read(chunks.packet_table,
                                      chunks.packet_table_size,
                                      &table_position, &encoded_size);
        if (rc != QW_STATUS_OK) return rc;
        if (!encoded_size || encoded_size > (uint64_t)INT_MAX ||
            encoded_size > (uint64_t)SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet_size = (size_t)encoded_size;
        if (data_position > chunks.audio_data_size ||
            packet_size > chunks.audio_data_size - data_position) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = caf_amr_packet_validate(chunks.audio_data + data_position,
                                     packet_size);
        if (rc != QW_STATUS_OK) return rc;
        data_position += packet_size;
    }
    if (data_position != chunks.audio_data_size) return QW_STATUS_ERR_FORMAT;
    while (table_position < chunks.packet_table_size) {
        if (chunks.packet_table[table_position++] != 0u)
            return QW_STATUS_ERR_UNSUPPORTED;
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_size_table = chunks.packet_table +
                                QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    source->packet_size_table_size = chunks.packet_table_size -
                                     QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    source->packet_count = (size_t)packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->extradata = chunks.magic_cookie;
    source->extradata_size = chunks.magic_cookie_size;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_amr_next_frame(const QwAudioCafAmrSource *source,
                                size_t packet_index,
                                size_t data_offset,
                                size_t packet_size_offset,
                                QwAudioCafAmrFrame *frame)
{
    uint64_t encoded_size;
    size_t next_packet_size_offset = packet_size_offset;
    size_t packet_size;
    int rc;

    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (packet_index >= source->packet_count) return 0;
    if (!source->data || !source->packet_size_table ||
        data_offset > source->data_size ||
        packet_size_offset > source->packet_size_table_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_caf_varint_read(source->packet_size_table,
                                  source->packet_size_table_size,
                                  &next_packet_size_offset, &encoded_size);
    if (rc != QW_STATUS_OK) return rc;
    if (!encoded_size || encoded_size > (uint64_t)SIZE_MAX ||
        encoded_size > (uint64_t)INT_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_size = (size_t)encoded_size;
    if (packet_size > source->data_size - data_offset)
        return QW_STATUS_ERR_FORMAT;
    rc = caf_amr_packet_validate(source->data + data_offset, packet_size);
    if (rc != QW_STATUS_OK) return rc;
    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = CAF_AMR_FRAMES_PER_PACKET;
    return 1;
}
