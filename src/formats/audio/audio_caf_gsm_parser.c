#include "audio_caf_gsm_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    CAF_GSM_DESCRIPTION_SIZE = 32u,
    CAF_GSM_PACKET_SIZE = 33u,
    CAF_GSM_FRAMES_PER_PACKET = 160u,
    CAF_GSM_SAMPLE_RATE = 8000u
};

static int caf_gsm_parse_description(const uint8_t *bytes,
                                     size_t size,
                                     QwAudioCafGsmSource *source)
{
    uint64_t sample_rate_bits;
    double sample_rate;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_GSM_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "agsm", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    if (sample_rate != (double)CAF_GSM_SAMPLE_RATE ||
        qw_read_u32_be(bytes + 12u) != 0u ||
        qw_read_u32_be(bytes + 16u) != CAF_GSM_PACKET_SIZE ||
        qw_read_u32_be(bytes + 20u) != CAF_GSM_FRAMES_PER_PACKET ||
        qw_read_u32_be(bytes + 24u) != 1u ||
        qw_read_u32_be(bytes + 28u) != 0u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->sample_rate = CAF_GSM_SAMPLE_RATE;
    source->channels = 1u;
    source->packet_size = CAF_GSM_PACKET_SIZE;
    return QW_STATUS_OK;
}

int qw_audio_caf_gsm_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafGsmSource *source,
                           QwConvertReport *report)
{
    QwAudioCafChunks chunks = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t packet_count;
    uint64_t total_frames;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_gsm_parse_description(chunks.description,
                                   chunks.description_size, source);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size ||
        chunks.audio_data_size % source->packet_size != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet_count = chunks.audio_data_size / source->packet_size;
    if (!packet_count || packet_count > UINT64_MAX / CAF_GSM_FRAMES_PER_PACKET)
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    total_frames = (uint64_t)packet_count * CAF_GSM_FRAMES_PER_PACKET;

    if (chunks.packet_table) {
        if (chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_table.packet_count != 0u) return QW_STATUS_ERR_FORMAT;
        rc = qw_audio_caf_packet_table_validate_frames(
            &packet_table, packet_count, CAF_GSM_FRAMES_PER_PACKET);
        if (rc != QW_STATUS_OK) return rc;
        while (table_position < chunks.packet_table_size) {
            if (chunks.packet_table[table_position++] != 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
    } else {
        packet_table.valid_frame_count = total_frames;
    }

    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_gsm_next_frame(const QwAudioCafGsmSource *source,
                                size_t packet_index,
                                QwAudioCafGsmFrame *frame)
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
    frame->sample_count = CAF_GSM_FRAMES_PER_PACKET;
    return 1;
}
