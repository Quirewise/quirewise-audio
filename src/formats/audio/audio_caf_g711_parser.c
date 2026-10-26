#include "audio_caf_g711_parser.h"

#include "../../core/byte_utils.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    CAF_G711_DESCRIPTION_SIZE = 32u,
    CAF_G711_MAX_CHANNELS = 8u
};

typedef struct QwAudioCafG711Description {
    uint32_t sample_rate;
    uint32_t bytes_per_packet;
    uint16_t channels;
    QwAudioCafG711Codec codec;
} QwAudioCafG711Description;

static void caf_g711_note(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
}

static int caf_g711_parse_description(
    const uint8_t *bytes,
    size_t size,
    QwAudioCafG711Description *description,
    QwConvertReport *report)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t format_flags;
    uint32_t frames_per_packet;
    uint32_t channels;
    uint32_t bits_per_channel;

    if (!bytes || !description) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_G711_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "ulaw", 4u) == 0) {
        description->codec = QW_AUDIO_CAF_G711_MULAW;
    } else if (memcmp(bytes + 8u, "alaw", 4u) == 0) {
        description->codec = QW_AUDIO_CAF_G711_ALAW;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    format_flags = qw_read_u32_be(bytes + 12u);
    description->bytes_per_packet = qw_read_u32_be(bytes + 16u);
    frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    bits_per_channel = qw_read_u32_be(bytes + 28u);

    if (!(sample_rate >= 4000.0 && sample_rate <= 384000.0) ||
        (double)(uint32_t)sample_rate != sample_rate || format_flags != 0u ||
        channels == 0u || channels > CAF_G711_MAX_CHANNELS ||
        frames_per_packet != 1u || bits_per_channel != 0u ||
        description->bytes_per_packet != channels) {
        caf_g711_note(report,
                      "native CAF G.711 parser supports 1-8 channels, one frame per packet, and 4-384 kHz");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->sample_rate = (uint32_t)sample_rate;
    description->channels = (uint16_t)channels;
    return QW_STATUS_OK;
}

int qw_audio_caf_g711_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafG711Source *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks;
    QwAudioCafG711Description description = {0};
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t sample_count;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_g711_parse_description(chunks.description,
                                    chunks.description_size,
                                    &description, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.audio_data || !chunks.audio_data_size ||
        chunks.audio_data_size % description.bytes_per_packet != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_count = chunks.audio_data_size / description.bytes_per_packet;
    if (!sample_count || sample_count > (uint64_t)INT64_MAX)
        return QW_STATUS_ERR_UNSUPPORTED;
    packet_table.valid_frame_count = sample_count;
    if (chunks.packet_table) {
        if (chunks.packet_table_size !=
            QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        rc = qw_audio_caf_packet_table_parse_header(
            chunks.packet_table, chunks.packet_table_size, &packet_table);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_table.packet_count != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    }
    rc = qw_audio_caf_packet_table_validate_frames(&packet_table,
                                                   sample_count, 1u);
    if (rc != QW_STATUS_OK) return rc;

    source->data = chunks.audio_data;
    source->size = chunks.audio_data_size;
    source->encoded_frame_count = sample_count;
    source->sample_count = packet_table.valid_frame_count;
    source->sample_rate = description.sample_rate;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->channels = description.channels;
    source->codec = description.codec;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}
