#include "audio_caf_alac_parser.h"

#include "audio_caf_channel_layout_parser.h"
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
    ALAC_SPECIFIC_CONFIG_SIZE = 24u,
    ALAC_OLD_COOKIE_PREFIX_SIZE = 12u,
    ALAC_MAX_FRAMES_PER_PACKET = 1048576u,
    ALAC_MAX_CHANNELS = 2u
};

typedef struct QwAudioCafAlacDescription {
    uint32_t sample_rate;
    uint32_t bytes_per_packet;
    uint32_t frames_per_packet;
    uint16_t channels;
    uint16_t bits_per_sample;
} QwAudioCafAlacDescription;

static void caf_alac_note(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
}

static int caf_alac_parse_description(const uint8_t *bytes,
                                      size_t size,
                                      QwAudioCafAlacDescription *description,
                                      QwConvertReport *report)
{
    uint64_t sample_rate_bits;
    double sample_rate;
    uint32_t flags;
    uint32_t channels;
    uint32_t bits_per_sample;

    if (!bytes || !description) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != CAF_DESCRIPTION_SIZE) return QW_STATUS_ERR_FORMAT;
    if (memcmp(bytes + 8u, "alac", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    sample_rate_bits = qw_read_u64_be(bytes);
    memcpy(&sample_rate, &sample_rate_bits, sizeof(sample_rate));
    flags = qw_read_u32_be(bytes + 12u);
    description->bytes_per_packet = qw_read_u32_be(bytes + 16u);
    description->frames_per_packet = qw_read_u32_be(bytes + 20u);
    channels = qw_read_u32_be(bytes + 24u);
    bits_per_sample = qw_read_u32_be(bytes + 28u);

    if (!(sample_rate >= 1.0 && sample_rate <= 384000.0) ||
        (double)(uint32_t)sample_rate != sample_rate || flags != 0u ||
        channels == 0u || channels > ALAC_MAX_CHANNELS ||
        (bits_per_sample != 16u && bits_per_sample != 20u &&
         bits_per_sample != 24u && bits_per_sample != 32u) ||
        description->frames_per_packet == 0u ||
        description->frames_per_packet > ALAC_MAX_FRAMES_PER_PACKET ||
        description->bytes_per_packet > (uint32_t)INT_MAX) {
        caf_alac_note(report,
                      "native CAF ALAC parser supports 16/20/24/32-bit mono/stereo streams with fixed packet frames");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    description->sample_rate = (uint32_t)sample_rate;
    description->channels = (uint16_t)channels;
    description->bits_per_sample = (uint16_t)bits_per_sample;
    return QW_STATUS_OK;
}

static int caf_alac_validate_channel_layout(const uint8_t *layout,
                                           uint16_t channels)
{
    QwAudioCafChannelLayout parsed_layout = {0};
    int rc;

    if (!layout) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (qw_read_u32_be(layout) != ALAC_SPECIFIC_CONFIG_SIZE ||
        memcmp(layout + 4u, "chan", 4u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_caf_channel_layout_parse(
        layout + 12u, ALAC_SPECIFIC_CONFIG_SIZE - 12u, channels,
        &parsed_layout);
    if (rc != QW_STATUS_OK) return rc;
    return parsed_layout.channel_order_unspecified
        ? QW_STATUS_ERR_UNSUPPORTED : QW_STATUS_OK;
}

static int caf_alac_validate_channel_layout_payload(const uint8_t *layout,
                                                    size_t layout_size,
                                                    uint16_t channels)
{
    QwAudioCafChannelLayout parsed_layout = {0};
    int rc;

    if (!layout) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_caf_channel_layout_parse(layout, layout_size, channels,
                                           &parsed_layout);
    if (rc != QW_STATUS_OK) return rc;
    return parsed_layout.channel_order_unspecified
        ? QW_STATUS_ERR_UNSUPPORTED : QW_STATUS_OK;
}

static int caf_alac_normalize_cookie(const uint8_t *cookie,
                                    size_t cookie_size,
                                    uint16_t channels,
                                    uint8_t *extradata,
                                    const uint8_t **specific_config)
{
    size_t tail_size;
    int old_style;

    if (!cookie || !extradata || !specific_config)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (cookie_size < ALAC_SPECIFIC_CONFIG_SIZE)
        return QW_STATUS_ERR_FORMAT;
    old_style = cookie_size >= ALAC_OLD_COOKIE_PREFIX_SIZE +
                                   QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE &&
                qw_read_u32_be(cookie) == ALAC_OLD_COOKIE_PREFIX_SIZE &&
                memcmp(cookie + 4u, "frma", 4u) == 0 &&
                memcmp(cookie + 8u, "alac", 4u) == 0;
    if (old_style) {
        if (qw_read_u32_be(cookie + ALAC_OLD_COOKIE_PREFIX_SIZE) !=
                QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE ||
            memcmp(cookie + ALAC_OLD_COOKIE_PREFIX_SIZE + 4u, "alac", 4u) != 0 ||
            qw_read_u32_be(cookie + ALAC_OLD_COOKIE_PREFIX_SIZE + 8u) != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        tail_size = cookie_size - ALAC_OLD_COOKIE_PREFIX_SIZE -
                    QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE;
        if (tail_size == 8u) {
            const uint8_t *terminator = cookie + cookie_size - 8u;
            if (qw_read_u32_be(terminator) != 8u ||
                qw_read_u32_be(terminator + 4u) != 0u) {
                return QW_STATUS_ERR_FORMAT;
            }
        } else if (tail_size != 0u) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        memcpy(extradata, cookie + ALAC_OLD_COOKIE_PREFIX_SIZE,
               QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE);
        *specific_config = extradata + 12u;
        return QW_STATUS_OK;
    }

    tail_size = cookie_size - ALAC_SPECIFIC_CONFIG_SIZE;
    if (tail_size != 0u && tail_size != ALAC_SPECIFIC_CONFIG_SIZE)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (tail_size == ALAC_SPECIFIC_CONFIG_SIZE) {
        int rc = caf_alac_validate_channel_layout(
            cookie + ALAC_SPECIFIC_CONFIG_SIZE, channels);
        if (rc != QW_STATUS_OK) return rc;
    }
    qw_write_u32_be(extradata, QW_AUDIO_CAF_ALAC_EXTRADATA_SIZE);
    memcpy(extradata + 4u, "alac", 4u);
    memset(extradata + 8u, 0, 4u);
    memcpy(extradata + 12u, cookie, ALAC_SPECIFIC_CONFIG_SIZE);
    *specific_config = extradata + 12u;
    return QW_STATUS_OK;
}

static int caf_alac_validate_specific_config(
    const uint8_t *specific_config,
    const QwAudioCafAlacDescription *description,
    uint32_t *max_frame_bytes)
{
    if (!specific_config || !description || !max_frame_bytes)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (qw_read_u32_be(specific_config) != description->frames_per_packet ||
        specific_config[4] != 0u ||
        specific_config[5] != description->bits_per_sample ||
        specific_config[9] != description->channels ||
        qw_read_u32_be(specific_config + 20u) != description->sample_rate) {
        return QW_STATUS_ERR_FORMAT;
    }
    *max_frame_bytes = qw_read_u32_be(specific_config + 12u);
    return QW_STATUS_OK;
}

static int caf_alac_validate_packet_table(const QwAudioCafChunks *chunks,
                                          const QwAudioCafAlacDescription *description,
                                          QwAudioCafAlacSource *source)
{
    QwAudioCafPacketTableHeader packet_table = {0};
    uint64_t declared_packet_count;
    size_t table_position = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
    size_t data_position = 0u;
    size_t packet_count;
    size_t i;
    int has_packet_table = chunks->packet_table != NULL;

    if (!chunks->audio_data || !chunks->audio_data_size)
        return QW_STATUS_ERR_FORMAT;
    if (description->bytes_per_packet) {
        if (source->max_frame_bytes &&
            description->bytes_per_packet > source->max_frame_bytes) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (chunks->audio_data_size % description->bytes_per_packet != 0u)
            return QW_STATUS_ERR_FORMAT;
        packet_count = chunks->audio_data_size /
                       description->bytes_per_packet;
        if (!packet_count) return QW_STATUS_ERR_FORMAT;
        source->fixed_packet_size = description->bytes_per_packet;
        if (has_packet_table) {
            if (chunks->packet_table_size <
                QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            {
                int rc = qw_audio_caf_packet_table_parse_header(
                    chunks->packet_table, chunks->packet_table_size,
                    &packet_table);
                if (rc != QW_STATUS_OK) return rc;
            }
            declared_packet_count = packet_table.packet_count;
            if (declared_packet_count != 0u &&
                declared_packet_count != (uint64_t)packet_count) {
                return QW_STATUS_ERR_FORMAT;
            }
            for (i = QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
                 i < chunks->packet_table_size; ++i) {
                if (chunks->packet_table[i] != 0u)
                    return QW_STATUS_ERR_UNSUPPORTED;
            }
        } else {
            if ((uint64_t)packet_count >
                UINT64_MAX / description->frames_per_packet) {
                return QW_STATUS_ERR_FORMAT;
            }
            packet_table.valid_frame_count = (uint64_t)packet_count *
                                             description->frames_per_packet;
        }
    } else {
        if (!has_packet_table ||
            chunks->packet_table_size <
                QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        {
            int rc = qw_audio_caf_packet_table_parse_header(
                chunks->packet_table, chunks->packet_table_size,
                &packet_table);
            if (rc != QW_STATUS_OK) return rc;
        }
        declared_packet_count = packet_table.packet_count;
        if (declared_packet_count == 0u ||
            declared_packet_count > (uint64_t)SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet_count = (size_t)declared_packet_count;
        source->packet_size_table = chunks->packet_table +
                                    QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
        source->packet_size_table_size = chunks->packet_table_size -
                                         QW_AUDIO_CAF_PACKET_TABLE_HEADER_SIZE;
        source->variable_packet_sizes = 1;
        for (i = 0u; i < packet_count; ++i) {
            uint64_t encoded_size;
            size_t packet_size;
            int rc = qw_audio_caf_varint_read(
                source->packet_size_table, source->packet_size_table_size,
                &table_position, &encoded_size);
            if (rc != QW_STATUS_OK) return rc;
            if (encoded_size == 0u || encoded_size > (uint64_t)SIZE_MAX)
                return QW_STATUS_ERR_FORMAT;
            if (encoded_size > (uint64_t)INT_MAX)
                return QW_STATUS_ERR_UNSUPPORTED;
            packet_size = (size_t)encoded_size;
            if (source->max_frame_bytes &&
                packet_size > source->max_frame_bytes) {
                return QW_STATUS_ERR_FORMAT;
            }
            if (packet_size > chunks->audio_data_size - data_position)
                return QW_STATUS_ERR_FORMAT;
            data_position += packet_size;
        }
        if (data_position != chunks->audio_data_size)
            return QW_STATUS_ERR_FORMAT;
        while (table_position < source->packet_size_table_size) {
            if (source->packet_size_table[table_position++] != 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
    }

    {
        int rc = qw_audio_caf_packet_table_validate_frames(
            &packet_table, (uint64_t)packet_count,
            description->frames_per_packet);
        if (rc != QW_STATUS_OK) return rc;
    }
    source->packet_count = packet_count;
    source->sample_count = packet_table.valid_frame_count;
    source->priming_frames = packet_table.priming_frames;
    source->remainder_frames = packet_table.remainder_frames;
    return QW_STATUS_OK;
}

int qw_audio_caf_alac_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafAlacSource *source,
                            QwConvertReport *report)
{
    QwAudioCafChunks chunks;
    QwAudioCafAlacDescription description = {0};
    const uint8_t *specific_config = NULL;
    uint32_t max_frame_bytes;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_caf_parse_chunks(bytes, size, &chunks);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_alac_parse_description(chunks.description,
                                    chunks.description_size,
                                    &description, report);
    if (rc != QW_STATUS_OK) return rc;
    if (chunks.channel_layout) {
        rc = caf_alac_validate_channel_layout_payload(
            chunks.channel_layout, chunks.channel_layout_size,
            description.channels);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (!chunks.magic_cookie || !chunks.magic_cookie_size)
        return QW_STATUS_ERR_FORMAT;
    rc = caf_alac_normalize_cookie(chunks.magic_cookie,
                                   chunks.magic_cookie_size,
                                   description.channels,
                                   source->extradata,
                                   &specific_config);
    if (rc != QW_STATUS_OK) return rc;
    rc = caf_alac_validate_specific_config(specific_config, &description,
                                           &max_frame_bytes);
    if (rc != QW_STATUS_OK) return rc;
    source->max_frame_bytes = max_frame_bytes;

    source->sample_rate = description.sample_rate;
    source->frames_per_packet = description.frames_per_packet;
    source->channels = description.channels;
    source->bits_per_sample = description.bits_per_sample;
    rc = caf_alac_validate_packet_table(&chunks, &description, source);
    if (rc != QW_STATUS_OK) return rc;
    source->data = chunks.audio_data;
    source->data_size = chunks.audio_data_size;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}

int qw_audio_caf_alac_next_frame(const QwAudioCafAlacSource *source,
                                 size_t packet_index,
                                 size_t data_offset,
                                 size_t packet_size_offset,
                                 QwAudioCafAlacFrame *frame)
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
        if (encoded_size == 0u || encoded_size > (uint64_t)SIZE_MAX ||
            encoded_size > (uint64_t)INT_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        packet_size = (size_t)encoded_size;
    } else {
        packet_size = source->fixed_packet_size;
    }
    if (!packet_size || packet_size > source->data_size - data_offset ||
        (source->max_frame_bytes && packet_size > source->max_frame_bytes)) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame->data = source->data + data_offset;
    frame->size = packet_size;
    frame->next_data_offset = data_offset + packet_size;
    frame->next_packet_size_offset = next_packet_size_offset;
    frame->sample_count = source->frames_per_packet;
    return 1;
}
