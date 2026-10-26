#include "audio_caf_qcelp_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "audio_caf_varint.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_QCELP_DESCRIPTION_SIZE = 32u,
    CAF_QCELP_SAMPLE_RATE = 8000u,
    CAF_QCELP_FRAMES_PER_PACKET = 160u,
    CAF_QCELP_FIXED_PACKET_LOW_RATE = 17u,
    CAF_QCELP_FIXED_PACKET_FULL_RATE = 35u
};

static int caf_qcelp_packet_size_supported(uint64_t size)
{
    return size == CAF_QCELP_FIXED_PACKET_LOW_RATE ||
           size == CAF_QCELP_FIXED_PACKET_FULL_RATE;
}

static int caf_qcelp_parse_description(const uint8_t *bytes,
                                       size_t size,
                                       QwAudioCafQcelpSource *source)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t bytes_per_packet;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_QCELP_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "Qclp", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    if (sample_rate != (double)CAF_QCELP_SAMPLE_RATE ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        (bytes_per_packet != 0u &&
         !caf_qcelp_packet_size_supported(bytes_per_packet)) ||
        qw_read_u32_be(bytes + 20u) != CAF_QCELP_FRAMES_PER_PACKET ||
        qw_read_u32_be(bytes + 24u) != 1u ||
        qw_read_u32_be(bytes + 28u) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->fixed_packet_size = bytes_per_packet;
    return QW_STATUS_OK;
}

int qw_audio_caf_qcelp_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioCafQcelpSource *source,
                             QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t packet_count;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    size_t fixed_packet_size;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_qcelp_parse_description(chunks.description,
                                     chunks.description_size, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size) return QW_STATUS_ERR_FORMAT;
    fixed_packet_size = source->fixed_packet_size;

    if (fixed_packet_size) {
        if (chunks.audio_data_size % fixed_packet_size != 0u)
            return QW_STATUS_ERR_FORMAT;
        packet_count = chunks.audio_data_size / fixed_packet_size;
        if (!packet_count) return QW_STATUS_ERR_FORMAT;
        if (chunks.packet_table) {
            if (chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_caf_packet_table_parse_header(
                chunks.packet_table, chunks.packet_table_size, &packet_table);
            if (rc != QW_STATUS_OK) return rc;
            if (packet_table.packet_count != 0u) return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_caf_packet_table_validate_frames(
                &packet_table, packet_count, CAF_QCELP_FRAMES_PER_PACKET);
            if (rc != QW_STATUS_OK) return rc;
            while (table_position < chunks.packet_table_size) {
                if (chunks.packet_table[table_position++] != 0u)
                    return QW_STATUS_ERR_UNSUPPORTED;
            }
        } else {
            if (packet_count > UINT64_MAX / CAF_QCELP_FRAMES_PER_PACKET)
                return QW_STATUS_ERR_RESOURCE_LIMIT;
            packet_table.valid_frame_count =
                packet_count * CAF_QCELP_FRAMES_PER_PACKET;
        }
    } else {
        uint64_t i;

        if (!chunks.packet_table ||
            chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        packet_count = packet_table.packet_count;
        if (!packet_count || packet_count > (uint64_t)SIZE_MAX)
            return QW_STATUS_ERR_FORMAT;
        rc = qw_audio_caf_packet_table_validate_frames(
            &packet_table, packet_count, CAF_QCELP_FRAMES_PER_PACKET);
        if (rc != QW_STATUS_OK) return rc;
        for (i = 0u; i < packet_count; ++i) {
            uint64_t encoded_size;
            size_t packet_size;
            rc = qw_audio_caf_varint_read(chunks.packet_table,
                                          chunks.packet_table_size,
                                          &table_position, &encoded_size);
            if (rc != QW_STATUS_OK) return rc;
            if (!caf_qcelp_packet_size_supported(encoded_size))
                return QW_STATUS_ERR_UNSUPPORTED;
            packet_size = (size_t)encoded_size;
            if (data_position > chunks.audio_data_size ||
                packet_size > chunks.audio_data_size - data_position) {
                return QW_STATUS_ERR_FORMAT;
            }
            data_position += packet_size;
        }
        if (data_position != chunks.audio_data_size) return QW_STATUS_ERR_FORMAT;
        while (table_position < chunks.packet_table_size) {
            if (chunks.packet_table[table_position++] != 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_count = (size_t)packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->packet_size_table = fixed_packet_size ? NULL :
        chunks.packet_table + QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    source->packet_size_table_size = fixed_packet_size ? 0u :
        chunks.packet_table_size - QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    source->extradata = chunks.magic_cookie;
    source->extradata_size = chunks.magic_cookie_size;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_qcelp_next_frame(const QwAudioCafQcelpSource *source,
                                  size_t packet_index,
                                  size_t data_offset,
                                  size_t packet_size_offset,
                                  QwAudioCafQcelpFrame *frame)
{
    uint64_t encoded_size;
    size_t next_packet_size_offset = packet_size_offset;
    size_t packet_size;

    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (packet_index >= source->packet_count) return 0;
    if (!source->data || data_offset > source->data_size)
        return QW_STATUS_ERR_FORMAT;
    if (source->fixed_packet_size) {
        packet_size = source->fixed_packet_size;
    } else {
        int rc;
        if (!source->packet_size_table ||
            packet_size_offset > source->packet_size_table_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_varint_read(source->packet_size_table,
                                      source->packet_size_table_size,
                                      &next_packet_size_offset, &encoded_size);
        if (rc != QW_STATUS_OK) return rc;
        if (!caf_qcelp_packet_size_supported(encoded_size))
            return QW_STATUS_ERR_FORMAT;
        packet_size = (size_t)encoded_size;
    }
    if (packet_size > source->data_size - data_offset)
        return QW_STATUS_ERR_FORMAT;
    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = CAF_QCELP_FRAMES_PER_PACKET;
    return 1;
}
