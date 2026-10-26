#include "audio_caf_qdesign_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

enum {
    CAF_QDESIGN_DESCRIPTION_SIZE = 32u,
    CAF_QDESIGN_MAX_CHANNELS = 2u,
    CAF_QDESIGN_FRAMES_PER_CHANNEL = 2048u,
    CAF_QDESIGN_MAX_EXTRADATA_SIZE = 1024u * 1024u
};

static int caf_qdesign_parse_description(const uint8_t *bytes,
                                         size_t size,
                                         QwAudioCafQdesignSource *source)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint32_t channels;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_QDESIGN_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "QDMC", 4u) == 0) {
        source->codec = QW_AUDIO_CAF_QDESIGN_QDMC;
    } else if (memcmp(bytes + 8u, "QDM2", 4u) == 0) {
        source->codec = QW_AUDIO_CAF_QDESIGN_QDM2;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    if (!(sample_rate >= 8000.0 && sample_rate <= 384000.0) ||
        (double)(uint32_t)sample_rate != sample_rate ||
        qw_read_u32_be(bytes + 12u) != 0u || !bytes_per_packet ||
        bytes_per_packet > (uint32_t)INT_MAX || channels == 0u ||
        channels > CAF_QDESIGN_MAX_CHANNELS ||
        frames_per_packet != CAF_QDESIGN_FRAMES_PER_CHANNEL * channels ||
        qw_read_u32_be(bytes + 28u) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->sample_rate = (uint32_t)sample_rate;
    source->channels = (uint16_t)channels;
    source->packet_size = bytes_per_packet;
    source->frames_per_packet = frames_per_packet;
    return QW_STATUS_OK;
}

int qw_audio_caf_qdesign_parse(const uint8_t *bytes,
                               size_t size,
                               QwAudioCafQdesignSource *source,
                               QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    size_t packet_count;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    uint64_t total_frames;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_qdesign_parse_description(chunks.description,
                                       chunks.description_size, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size ||
        chunks.audio_data_size % source->packet_size != 0u ||
        !chunks.magic_cookie || !chunks.magic_cookie_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (chunks.magic_cookie_size > CAF_QDESIGN_MAX_EXTRADATA_SIZE)
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    packet_count = chunks.audio_data_size / source->packet_size;
    if (!packet_count ||
        (uint64_t)packet_count > UINT64_MAX / source->frames_per_packet) {
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    }
    total_frames = (uint64_t)packet_count * source->frames_per_packet;

    if (chunks.packet_table) {
        if (chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
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
        if (total_frames > (uint64_t)INT64_MAX)
            return QW_STATUS_ERR_RESOURCE_LIMIT;
        packet_table.valid_frame_count = total_frames;
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->extradata = chunks.magic_cookie;
    source->extradata_size = chunks.magic_cookie_size;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_qdesign_next_frame(const QwAudioCafQdesignSource *source,
                                    size_t packet_index,
                                    QwAudioCafQdesignFrame *frame)
{
    size_t offset;

    if (!source || !frame || !source->data || !source->packet_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (packet_index >= source->packet_count) return 0;
    if (packet_index > SIZE_MAX / source->packet_size)
        return QW_STATUS_ERR_FORMAT;
    offset = packet_index * source->packet_size;
    if (offset > source->data_size ||
        source->packet_size > source->data_size - offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->data + offset;
    frame->size = source->packet_size;
    frame->next_packet_index = packet_index + 1u;
    frame->sample_count = source->frames_per_packet;
    return 1;
}
