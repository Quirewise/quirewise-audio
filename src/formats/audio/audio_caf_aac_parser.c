#include "audio_caf_aac_parser.h"

#include "audio_aac_specific_config_parser.h"
#include "audio_mpeg4_esds_parser.h"
#include "audio_caf_chunks.h"
#include "audio_caf_packet_table_parser.h"
#include "audio_caf_varint.h"
#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    CAF_DESCRIPTION_SIZE = 32u,
    CAF_AAC_LC_OBJECT_TYPE = 2u,
    CAF_AAC_LD_OBJECT_TYPE = 23u,
    CAF_AAC_MAX_CHANNELS = 8u
};

typedef struct QwAudioCafAacDescription {
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint16_t channels;
    uint16_t object_type;
    uint8_t channel_configuration;
    uint8_t ps_present;
} QwAudioCafAacDescription;

static void caf_aac_note(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
}

static int caf_aac_find_decoder_specific_info(const uint8_t *cookie,
                                               size_t cookie_size,
                                               const uint8_t **specific_info,
                                               size_t *specific_info_size)
{
    return qw_audio_mpeg4_esds_find_aac_specific_info(
        cookie, cookie_size,
        QW_AUDIO_MPEG4_ESDS_ALLOW_ATOM |
        QW_AUDIO_MPEG4_ESDS_ALLOW_FULLBOX |
        QW_AUDIO_MPEG4_ESDS_ALLOW_ZERO_PADDING,
        specific_info, specific_info_size);
}

static int caf_aac_parse_specific_info(const uint8_t *bytes,
                                      size_t size,
                                      QwAudioCafAacDescription *description,
                                      QwConvertReport *report)
{
    QwAudioAacSpecificConfig parsed;
    uint32_t output_sample_rate;
    uint32_t output_frames_per_packet;
    uint16_t channels;
    int rc;

    if (!bytes || !description) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_aac_specific_config_parse(bytes, size, &parsed);
    if (rc != QW_STATUS_OK) return rc;
    if (parsed.object_type != description->object_type)
        return QW_STATUS_ERR_FORMAT;

    output_sample_rate = parsed.output_sample_rate;
    output_frames_per_packet = parsed.core_frames_per_packet;
    if (parsed.sbr_present) {
        if (output_sample_rate == parsed.core_sample_rate) {
            /* Down-sampled SBR keeps the core packet frame count. */
        } else if (parsed.core_sample_rate <= UINT32_MAX / 2u &&
                   output_sample_rate == parsed.core_sample_rate * 2u) {
            output_frames_per_packet *= 2u;
        } else {
            caf_aac_note(report,
                         "native CAF AAC parser supports equal-rate or 2x SBR output");
            return QW_STATUS_ERR_UNSUPPORTED;
        }
    } else if (!parsed.sbr_signaled &&
               parsed.object_type == CAF_AAC_LC_OBJECT_TYPE &&
               parsed.core_sample_rate <= UINT32_MAX / 2u &&
               parsed.core_frames_per_packet <= UINT32_MAX / 2u &&
               description->sample_rate == parsed.core_sample_rate * 2u &&
               description->frames_per_packet ==
                   parsed.core_frames_per_packet * 2u) {
        /* CAF metadata can identify backward-compatible implicit SBR. */
        output_sample_rate = description->sample_rate;
        output_frames_per_packet *= 2u;
    } else {
        output_sample_rate = parsed.core_sample_rate;
    }

    if (parsed.ps_present && parsed.core_channels != 1u) {
        caf_aac_note(report,
                     "native CAF AAC parser requires a mono AAC core for PS");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    channels = parsed.ps_present ? 2u : parsed.core_channels;
    if (output_sample_rate != description->sample_rate ||
        channels != description->channels ||
        output_frames_per_packet != description->frames_per_packet) {
        return QW_STATUS_ERR_FORMAT;
    }
    description->channel_configuration = parsed.channel_configuration;
    description->ps_present = parsed.ps_present;
    return QW_STATUS_OK;
}

static int caf_aac_parse_description(const uint8_t *bytes,
                                     size_t size,
                                     QwAudioCafAacDescription *description,
                                     QwConvertReport *report)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t flags;
    uint32_t bytes_per_packet;
    uint32_t channels;
    uint32_t sample_bits;
    uint32_t object_type;

    int is_low_delay;

    if (!bytes || !description) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    is_low_delay = memcmp(bytes + 8u, "aacl", 4u) == 0;
    if (!is_low_delay && memcmp(bytes + 8u, "aac ", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    flags = qw_read_u32_be(bytes + 12u);
    bytes_per_packet = qw_read_u32_be(bytes + 16u);
    description->frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    sample_bits = qw_read_u32_be(bytes + 28u);
    object_type = is_low_delay ? CAF_AAC_LD_OBJECT_TYPE : flags & 0x1fu;

    if (!(sample_rate >= 7350.0 && sample_rate <= 96000.0) ||
        (double)(uint32_t)sample_rate != sample_rate ||
        bytes_per_packet != 0u || sample_bits != 0u ||
        channels == 0u || channels > CAF_AAC_MAX_CHANNELS ||
        (is_low_delay
             ? (flags != 0u ||
                (description->frames_per_packet != 480u &&
                 description->frames_per_packet != 512u))
             : (object_type < 1u || object_type > 4u ||
                flags != object_type ||
                (description->frames_per_packet != 960u &&
                 description->frames_per_packet != 1024u &&
                 description->frames_per_packet != 1920u &&
                 description->frames_per_packet != 2048u)))) {
        caf_aac_note(report, "native CAF AAC parser supports AAC Main/LC/SSR/LTP and AAC-LD with up to 8 channels at 7.35-96 kHz");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->sample_rate = (uint32_t)sample_rate;
    description->channels = (uint16_t)channels;
    description->object_type = (uint16_t)object_type;
    return QW_STATUS_OK;
}

static int caf_aac_read_packet_size(const uint8_t *bytes,
                                    size_t size,
                                    size_t *position,
                                    size_t *packet_size)
{
    uint64_t value;
    int rc;
    if (!bytes || !position || !packet_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_caf_varint_read(bytes, size, position, &value);
    if (rc != QW_STATUS_OK) return rc;
    if (value == 0u || value > (uint64_t)SIZE_MAX) return QW_STATUS_ERR_FORMAT;
    if (value > (uint64_t)INT_MAX) return QW_STATUS_ERR_UNSUPPORTED;
    *packet_size = (size_t)value;
    return QW_STATUS_OK;
}

int qw_audio_caf_aac_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafAacSource *source,
                           QwConvertReport *report)
{
    QwAudioCafChunks chunks;
    QwAudioCafAacDescription description = {0};
    const uint8_t *specific_info = NULL;
    size_t specific_info_size = 0u;
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t packet_count;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    size_t i;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_aac_parse_description(chunks.description,
                                   chunks.description_size,
                                   &description, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.packet_table ||
        chunks.packet_table_size < QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE ||
        !chunks.magic_cookie || !chunks.magic_cookie_size ||
        !chunks.audio_data || !chunks.audio_data_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = caf_aac_find_decoder_specific_info(chunks.magic_cookie,
                                             chunks.magic_cookie_size,
                                             &specific_info,
                                             &specific_info_size);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_aac_parse_specific_info(specific_info, specific_info_size,
                                     &description, report);
    if (rc != QW_STATUS_OK) return rc;

    rc = qw_audio_caf_packet_table_parse_header(
        chunks.packet_table, chunks.packet_table_size, &packet_table);
    if (rc != QW_STATUS_OK) return rc;
    packet_count = packet_table.packet_count;
    if (packet_count == 0u || packet_count > (uint64_t)SIZE_MAX)
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_caf_packet_table_validate_frames(
        &packet_table, packet_count, description.frames_per_packet);
    if (rc != QW_STATUS_OK) return rc;

    for (i = 0u; i < (size_t)packet_count; ++i) {
        size_t packet_size;
        rc = caf_aac_read_packet_size(chunks.packet_table,
                                      chunks.packet_table_size,
                                      &table_position, &packet_size);
        if (rc != QW_STATUS_OK) return rc;
        if (packet_size > chunks.audio_data_size - data_position)
            return QW_STATUS_ERR_FORMAT;
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
    source->audio_specific_config = specific_info;
    source->audio_specific_config_size = specific_info_size;
    source->packet_count = (size_t)packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->sample_rate = description.sample_rate;
    source->frames_per_packet = description.frames_per_packet;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    source->channels = description.channels;
    source->object_type = description.object_type;
    source->channel_configuration = description.channel_configuration;
    source->ps_present = description.ps_present;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_aac_next_frame(const QwAudioCafAacSource *source,
                                size_t packet_index,
                                size_t data_offset,
                                size_t packet_size_offset,
                                QwAudioCafAacFrame *frame)
{
    size_t packet_size;
    size_t next_packet_size_offset = packet_size_offset;
    int rc;
    if (!source || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (packet_index >= source->packet_count) return 0;
    if (!source->data || !source->packet_size_table ||
        data_offset > source->data_size ||
        packet_size_offset > source->packet_size_table_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = caf_aac_read_packet_size(source->packet_size_table,
                                  source->packet_size_table_size,
                                  &next_packet_size_offset, &packet_size);
    if (rc != QW_STATUS_OK) return rc;
    if (packet_size > source->data_size - data_offset)
        return QW_STATUS_ERR_FORMAT;
    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = source->frames_per_packet;
    return 1;
}
