#include "audio_caf_ilbc_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "audio_caf_varint.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    CAF_ILBC_DESCRIPTION_SIZE = 32u,
    CAF_ILBC_SAMPLE_RATE = 8000u,
    CAF_ILBC_MONO_CHANNELS = 1u,
    CAF_ILBC_20MS_PACKET_SIZE = 38u,
    CAF_ILBC_20MS_FRAMES_PER_PACKET = 160u,
    CAF_ILBC_30MS_PACKET_SIZE = 50u,
    CAF_ILBC_30MS_FRAMES_PER_PACKET = 240u
};

static int caf_ilbc_parse_description(const uint8_t *bytes,
                                      size_t size,
                                      QwAudioCafIlbcSource *source)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t frames_per_packet;
    uint32_t bytes_per_packet;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_ILBC_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "ilbc", 4u) != 0) return QW_STATUS_ERR_UNSUPPORTED;

    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    frames_per_packet = qw_read_u32_be(bytes + 20u);
    if (sample_rate != (double)CAF_ILBC_SAMPLE_RATE ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        qw_read_u32_be(bytes + 24u) != CAF_ILBC_MONO_CHANNELS ||
        qw_read_u32_be(bytes + 28u) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    source->sample_rate = CAF_ILBC_SAMPLE_RATE;
    source->channels = CAF_ILBC_MONO_CHANNELS;
    if (bytes_per_packet == 0u && frames_per_packet == 0u) {
        source->variable_packet_sizes = 1;
    } else if (bytes_per_packet == CAF_ILBC_20MS_PACKET_SIZE &&
               frames_per_packet == CAF_ILBC_20MS_FRAMES_PER_PACKET) {
        source->packet_size = CAF_ILBC_20MS_PACKET_SIZE;
        source->frames_per_packet = CAF_ILBC_20MS_FRAMES_PER_PACKET;
    } else if (bytes_per_packet == CAF_ILBC_30MS_PACKET_SIZE &&
               frames_per_packet == CAF_ILBC_30MS_FRAMES_PER_PACKET) {
        source->packet_size = CAF_ILBC_30MS_PACKET_SIZE;
        source->frames_per_packet = CAF_ILBC_30MS_FRAMES_PER_PACKET;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

static int caf_ilbc_frames_for_packet_size(uint64_t packet_size,
                                           uint32_t *frames_per_packet)
{
    if (!frames_per_packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet_size == CAF_ILBC_20MS_PACKET_SIZE) {
        *frames_per_packet = CAF_ILBC_20MS_FRAMES_PER_PACKET;
        return QW_STATUS_OK;
    }
    if (packet_size == CAF_ILBC_30MS_PACKET_SIZE) {
        *frames_per_packet = CAF_ILBC_30MS_FRAMES_PER_PACKET;
        return QW_STATUS_OK;
    }
    return QW_STATUS_ERR_FORMAT;
}

static int caf_ilbc_validate_variable_packet_table(
    const QwAudioCafChunks *chunks,
    const QwAudioCafPacketTableHeader *packet_table,
    size_t *packet_count_out)
{
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    size_t packet_count;
    uint64_t total_frames = 0u;
    uint32_t last_packet_frames = 0u;
    uint64_t i;
    int rc;

    if (!chunks || !packet_table || !packet_count_out ||
        !chunks->packet_table || !chunks->audio_data ||
        packet_table->packet_count == 0u ||
        packet_table->packet_count > (uint64_t)SIZE_MAX ||
        packet_table->packet_count > chunks->audio_data_size /
                                         CAF_ILBC_20MS_PACKET_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_count = (size_t)packet_table->packet_count;

    for (i = 0u; i < packet_table->packet_count; ++i) {
        uint64_t packet_size;
        uint32_t packet_frames;

        rc = qw_audio_caf_varint_read(chunks->packet_table,
                                      chunks->packet_table_size,
                                      &table_position, &packet_size);
        if (rc != QW_STATUS_OK) return rc;
        rc = caf_ilbc_frames_for_packet_size(packet_size, &packet_frames);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_size > (uint64_t)INT_MAX ||
            packet_size > (uint64_t)SIZE_MAX ||
            data_position > chunks->audio_data_size ||
            (size_t)packet_size > chunks->audio_data_size - data_position ||
            total_frames > UINT64_MAX - packet_frames) {
            return QW_STATUS_ERR_FORMAT;
        }
        data_position += (size_t)packet_size;
        total_frames += packet_frames;
        last_packet_frames = packet_frames;
    }
    if (data_position != chunks->audio_data_size ||
        packet_table->valid_frame_count == 0u ||
        packet_table->remainder_frames >= last_packet_frames ||
        packet_table->valid_frame_count >
            UINT64_MAX - packet_table->priming_frames ||
        packet_table->valid_frame_count + packet_table->priming_frames >
            UINT64_MAX - packet_table->remainder_frames ||
        packet_table->valid_frame_count + packet_table->priming_frames +
                packet_table->remainder_frames != total_frames) {
        return QW_STATUS_ERR_FORMAT;
    }
    while (table_position < chunks->packet_table_size) {
        if (chunks->packet_table[table_position++] != 0u)
            return QW_STATUS_ERR_UNSUPPORTED;
    }
    *packet_count_out = packet_count;
    return QW_STATUS_OK;
}

int qw_audio_caf_ilbc_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafIlbcSource *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    size_t packet_count = 0u;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    uint64_t total_frames;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));

    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_ilbc_parse_description(chunks.description,
                                    chunks.description_size, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size) {
        return QW_STATUS_ERR_FORMAT;
    }

    if (source->variable_packet_sizes) {
        if (!chunks.packet_table ||
            chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        rc = caf_ilbc_validate_variable_packet_table(&chunks, &packet_table,
                                                      &packet_count);
        if (rc != QW_STATUS_OK) return rc;
    } else {
        if (chunks.audio_data_size % source->packet_size != 0u)
            return QW_STATUS_ERR_FORMAT;
        packet_count = chunks.audio_data_size / source->packet_size;
        if (!packet_count ||
            packet_count > UINT64_MAX / source->frames_per_packet) {
            return QW_STATUS_ERR_RESOURCE_LIMIT;
        }
        total_frames = (uint64_t)packet_count * source->frames_per_packet;
        if (chunks.packet_table) {
            if (chunks.packet_table_size <
                QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
                return QW_STATUS_ERR_FORMAT;
            }
            rc = qw_audio_caf_packet_table_parse_header(
                chunks.packet_table, chunks.packet_table_size, &packet_table);
            if (rc != QW_STATUS_OK) return rc;
            if (packet_table.packet_count != 0u) return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_caf_packet_table_validate_frames(
                &packet_table, packet_count, source->frames_per_packet);
            if (rc != QW_STATUS_OK) return rc;
            while (table_position < chunks.packet_table_size) {
                if (chunks.packet_table[table_position++] != 0u)
                    return QW_STATUS_ERR_UNSUPPORTED;
            }
        } else {
            packet_table.valid_frame_count = total_frames;
        }
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    if (source->variable_packet_sizes) {
        source->packet_size_table = chunks.packet_table +
                                    QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
        source->packet_size_table_size = chunks.packet_table_size -
                                         QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    }
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_ilbc_next_frame(const QwAudioCafIlbcSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafIlbcFrame *frame)
{
    size_t packet_size;
    uint32_t frames_per_packet;
    size_t next_packet_size_offset = packet_size_offset;
    int rc;

    if (!source || !frame || !source->data) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (packet_index >= source->packet_count) return 0;
    if (source->variable_packet_sizes) {
        uint64_t encoded_packet_size;
        if (!source->packet_size_table ||
            data_offset > source->data_size ||
            packet_size_offset > source->packet_size_table_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_caf_varint_read(source->packet_size_table,
                                      source->packet_size_table_size,
                                      &next_packet_size_offset,
                                      &encoded_packet_size);
        if (rc != QW_STATUS_OK) return rc;
        rc = caf_ilbc_frames_for_packet_size(encoded_packet_size,
                                              &frames_per_packet);
        if (rc != QW_STATUS_OK || encoded_packet_size > (uint64_t)SIZE_MAX)
            return rc != QW_STATUS_OK ? rc : QW_STATUS_ERR_FORMAT;
        packet_size = (size_t)encoded_packet_size;
    } else {
        if (!source->packet_size || !source->frames_per_packet ||
            packet_index > SIZE_MAX / source->packet_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        data_offset = packet_index * source->packet_size;
        packet_size = source->packet_size;
        frames_per_packet = source->frames_per_packet;
    }
    if (data_offset > source->data_size ||
        packet_size > source->data_size - data_offset) {
        return QW_STATUS_ERR_FORMAT;
    }

    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = frames_per_packet;
    return 1;
}
