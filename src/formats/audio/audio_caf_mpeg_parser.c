#include "audio_caf_mpeg_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "audio_caf_varint.h"
#include "audio_mpeg_parser.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum { CAF_MPEG_DESCRIPTION_SIZE = 32u };

typedef struct QwAudioCafMpegDescription {
    uint32_t sample_rate;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint16_t channels;
    uint8_t layer;
} QwAudioCafMpegDescription;

static int caf_mpeg_parse_description(const uint8_t *bytes,
                                      size_t size,
                                      QwAudioCafMpegDescription *description)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    const uint8_t *format_id;

    if (!bytes || !description) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_MPEG_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    format_id = bytes + 8u;
    if (memcmp(format_id, ".mp1", 4u) == 0) {
        description->layer = 1u;
    } else if (memcmp(format_id, ".mp2", 4u) == 0) {
        description->layer = 2u;
    } else if (memcmp(format_id, ".mp3", 4u) == 0) {
        description->layer = 3u;
    } else if (memcmp(format_id, "ms\0U", 4u) == 0) {
        description->layer = 3u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    description->bytes_per_packet = qw_read_u32_be(bytes + 16u);
    description->frames_per_packet = qw_read_u32_be(bytes + 20u);
    description->channels = (uint16_t)qw_read_u32_be(bytes + 24u);
    if (!(sample_rate >= 8000.0 && sample_rate <= 48000.0) ||
        (double)(uint32_t)sample_rate != sample_rate ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        qw_read_u32_be(bytes + 28u) != 0u ||
        qw_read_u32_be(bytes + 24u) == 0u ||
        qw_read_u32_be(bytes + 24u) > 2u ||
        description->bytes_per_packet > (uint32_t)INT_MAX ||
        (description->frames_per_packet != 384u &&
         description->frames_per_packet != 576u &&
         description->frames_per_packet != 1152u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->sample_rate = (uint32_t)sample_rate;
    return QW_STATUS_OK;
}

static int caf_mpeg_packet_validate(const uint8_t *data,
                                    size_t size,
                                    const QwAudioCafMpegDescription *description)
{
    QwAudioMpegSource mpeg = {0};
    int rc;

    rc = qw_audio_mpeg_parse_layer_frames(data, size, description->layer,
                                         &mpeg, NULL);
    if (rc != QW_STATUS_OK) return rc;
    if (mpeg.frame_count != 1u ||
        mpeg.sample_count != description->frames_per_packet ||
        mpeg.sample_rate != description->sample_rate ||
        mpeg.channels != description->channels) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_caf_mpeg_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafMpegSource *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafMpegDescription description = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    size_t packet_count;
    size_t i;
    uint64_t total_frames;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_mpeg_parse_description(chunks.description,
                                    chunks.description_size, &description);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size ||
        !description.frames_per_packet) {
        return QW_STATUS_ERR_FORMAT;
    }

    if (description.bytes_per_packet) {
        if (chunks.audio_data_size % description.bytes_per_packet != 0u)
            return QW_STATUS_ERR_FORMAT;
        packet_count = chunks.audio_data_size / description.bytes_per_packet;
        if (!packet_count) return QW_STATUS_ERR_FORMAT;
        if (packet_count > UINT64_MAX / description.frames_per_packet)
            return QW_STATUS_ERR_RESOURCE_LIMIT;
        total_frames = (uint64_t)packet_count * description.frames_per_packet;
        if (chunks.packet_table) {
            if (chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_caf_packet_table_parse_header(
                chunks.packet_table, chunks.packet_table_size, &packet_table);
            if (rc != QW_STATUS_OK) return rc;
            if (packet_table.packet_count != 0u)
                return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_caf_packet_table_validate_frames(
                &packet_table, packet_count, description.frames_per_packet);
            if (rc != QW_STATUS_OK) return rc;
            while (table_position < chunks.packet_table_size) {
                if (chunks.packet_table[table_position++] != 0u)
                    return QW_STATUS_ERR_UNSUPPORTED;
            }
        } else {
            packet_table.valid_frame_count = total_frames;
        }
        source->fixed_packet_size = description.bytes_per_packet;
    } else {
        if (!chunks.packet_table ||
            chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        if (!packet_table.packet_count ||
            packet_table.packet_count > (uint64_t)SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet_count = (size_t)packet_table.packet_count;
        rc = qw_audio_caf_packet_table_validate_frames(
            &packet_table, packet_table.packet_count,
            description.frames_per_packet);
        if (rc != QW_STATUS_OK) return rc;
        for (i = 0u; i < packet_count; ++i) {
            uint64_t encoded_size;
            size_t packet_size;

            rc = qw_audio_caf_varint_read(
                chunks.packet_table, chunks.packet_table_size,
                &table_position, &encoded_size);
            if (rc != QW_STATUS_OK) return rc;
            if (!encoded_size || encoded_size > (uint64_t)INT_MAX ||
                encoded_size > (uint64_t)(chunks.audio_data_size - data_position)) {
                return QW_STATUS_ERR_FORMAT;
            }
            packet_size = (size_t)encoded_size;
            rc = caf_mpeg_packet_validate(chunks.audio_data + data_position,
                                          packet_size, &description);
            if (rc != QW_STATUS_OK) return rc;
            data_position += packet_size;
        }
        if (data_position != chunks.audio_data_size) return QW_STATUS_ERR_FORMAT;
        while (table_position < chunks.packet_table_size) {
            if (chunks.packet_table[table_position++] != 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
        source->packet_size_table = chunks.packet_table +
                                    QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
        source->packet_size_table_size = chunks.packet_table_size -
                                        QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
        source->variable_packet_sizes = 1;
    }

    if (description.bytes_per_packet) {
        for (i = 0u; i < packet_count; ++i) {
            size_t packet_size = description.bytes_per_packet;
            rc = caf_mpeg_packet_validate(chunks.audio_data + data_position,
                                          packet_size, &description);
            if (rc != QW_STATUS_OK) return rc;
            data_position += packet_size;
        }
        if (data_position != chunks.audio_data_size) return QW_STATUS_ERR_FORMAT;
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->sample_rate = description.sample_rate;
    source->frames_per_packet = description.frames_per_packet;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->channels = description.channels;
    source->layer = description.layer;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_mpeg_next_frame(const QwAudioCafMpegSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafMpegFrame *frame)
{
    size_t packet_size;
    size_t next_packet_size_offset = packet_size_offset;

    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (packet_index >= source->packet_count) return 0;
    if (!source->data || data_offset > source->data_size)
        return QW_STATUS_ERR_FORMAT;
    if (source->variable_packet_sizes) {
        uint64_t encoded_size;
        int rc;

        if (!source->packet_size_table ||
            packet_size_offset > source->packet_size_table_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_varint_read(
            source->packet_size_table, source->packet_size_table_size,
            &next_packet_size_offset, &encoded_size);
        if (rc != QW_STATUS_OK) return rc;
        if (!encoded_size || encoded_size > (uint64_t)INT_MAX ||
            encoded_size > (uint64_t)SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet_size = (size_t)encoded_size;
    } else {
        packet_size = source->fixed_packet_size;
    }
    if (!packet_size || packet_size > source->data_size - data_offset)
        return QW_STATUS_ERR_FORMAT;
    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = source->frames_per_packet;
    return 1;
}
