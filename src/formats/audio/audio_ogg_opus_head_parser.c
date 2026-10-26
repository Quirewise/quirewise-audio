#include "audio_ogg_opus_head_parser.h"

#include "audio_ogg_opus_parser.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

enum {
    AUDIO_OGG_OPUS_HEAD_SIZE = 19u,
    AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE = 21u
};

int qw_audio_ogg_opus_tags_validate(const uint8_t *packet,
                                    size_t packet_size)
{
    size_t position;
    uint32_t vendor_size;
    uint32_t comment_count;
    uint32_t i;
    if (!packet || packet_size < 16u ||
        memcmp(packet, "OpusTags", 8u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    vendor_size = qw_read_u32_le(packet + 8u);
    if ((size_t)vendor_size > packet_size - 12u) return QW_STATUS_ERR_FORMAT;
    position = 12u + (size_t)vendor_size;
    if (packet_size - position < 4u) return QW_STATUS_ERR_FORMAT;
    comment_count = qw_read_u32_le(packet + position);
    position += 4u;
    if ((uint64_t)comment_count > (uint64_t)(packet_size - position) / 4u)
        return QW_STATUS_ERR_FORMAT;
    for (i = 0u; i < comment_count; ++i) {
        uint32_t comment_size;
        if (packet_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        comment_size = qw_read_u32_le(packet + position);
        position += 4u;
        if ((size_t)comment_size > packet_size - position)
            return QW_STATUS_ERR_FORMAT;
        position += (size_t)comment_size;
    }
    return QW_STATUS_OK;
}

static int audio_ogg_opus_is_ambisonic_channel_count(uint8_t channels)
{
    uint16_t order;
    for (order = 1u; order <= 15u; ++order) {
        uint16_t ambisonic_channels = (uint16_t)(order * order);
        if (channels == ambisonic_channels ||
            channels == ambisonic_channels + 2u) {
            return 1;
        }
    }
    return 0;
}

static int audio_ogg_opus_parse_family3_head(
    const uint8_t *packet,
    size_t packet_size,
    QwAudioOggOpusSource *source)
{
    uint8_t output_channels;
    uint8_t stream_count;
    uint8_t coupled_count;
    uint16_t decoded_channels;
    size_t coefficient_count;
    size_t expected_size;
    size_t input_channel;
    double *demixing_matrix;

    if (packet_size < AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE)
        return QW_STATUS_ERR_FORMAT;
    output_channels = packet[9u];
    if (!audio_ogg_opus_is_ambisonic_channel_count(output_channels) ||
        output_channels > QW_AUDIO_OGG_OPUS_MAX_CHANNELS) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    stream_count = packet[AUDIO_OGG_OPUS_HEAD_SIZE];
    coupled_count = packet[AUDIO_OGG_OPUS_HEAD_SIZE + 1u];
    decoded_channels = (uint16_t)stream_count + coupled_count;
    if (!stream_count || coupled_count > stream_count ||
        decoded_channels > UINT8_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (decoded_channels > QW_AUDIO_OGG_OPUS_MAX_CHANNELS)
        return QW_STATUS_ERR_UNSUPPORTED;
    coefficient_count = (size_t)output_channels * decoded_channels;
    if (coefficient_count >
        (SIZE_MAX - AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE) / 2u) {
        return QW_STATUS_ERR_FORMAT;
    }
    expected_size = AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE +
                    coefficient_count * 2u;
    if (packet_size < expected_size) return QW_STATUS_ERR_FORMAT;
    if (packet_size != expected_size) return QW_STATUS_ERR_UNSUPPORTED;

    demixing_matrix = (double *)malloc(coefficient_count *
                                       sizeof(*demixing_matrix));
    if (!demixing_matrix) return QW_STATUS_ERR_OUT_OF_MEMORY;
    for (input_channel = 0u; input_channel < decoded_channels;
         ++input_channel) {
        size_t output_channel;
        for (output_channel = 0u; output_channel < output_channels;
             ++output_channel) {
            size_t coefficient_index =
                input_channel * output_channels + output_channel;
            uint16_t coefficient_bits = qw_read_u16_le(
                packet + AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE +
                coefficient_index * 2u);
            int32_t coefficient = coefficient_bits < 0x8000u
                ? (int32_t)coefficient_bits
                : (int32_t)coefficient_bits - 65536;
            demixing_matrix[output_channel * decoded_channels +
                            input_channel] =
                (double)coefficient / 32768.0;
        }
    }

    source->channels = output_channels;
    source->decoder_channels = decoded_channels;
    source->pre_skip = qw_read_u16_le(packet + 10u);
    source->mapping_family = 3u;
    source->demixing_matrix = demixing_matrix;
    source->demixing_matrix_stride = (int)decoded_channels;

    memcpy(source->opus_head, packet, AUDIO_OGG_OPUS_HEAD_SIZE);
    source->opus_head[9u] = (uint8_t)decoded_channels;
    source->opus_head[18u] = 255u;
    source->opus_head[19u] = stream_count;
    source->opus_head[20u] = coupled_count;
    for (input_channel = 0u; input_channel < decoded_channels;
         ++input_channel) {
        source->opus_head[AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE + input_channel] =
            (uint8_t)input_channel;
    }
    source->opus_head_size = AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE +
                             decoded_channels;
    return QW_STATUS_OK;
}

int qw_audio_ogg_opus_head_parse(const uint8_t *packet,
                                 size_t packet_size,
                                 struct QwAudioOggOpusSource *source)
{
    if (!packet || !source || packet_size < AUDIO_OGG_OPUS_HEAD_SIZE ||
        memcmp(packet, "OpusHead", 8u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    if ((packet[8u] & 0xf0u) != 0u || !packet[9u]) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (packet[18u] == 0u) {
        if (packet[9u] > 2u || packet_size != AUDIO_OGG_OPUS_HEAD_SIZE)
            return QW_STATUS_ERR_UNSUPPORTED;
    } else if (packet[18u] == 3u) {
        return audio_ogg_opus_parse_family3_head(packet, packet_size,
                                                 source);
    } else if (packet[18u] == 1u || packet[18u] == 2u ||
               packet[18u] == 255u) {
        uint16_t decoded_channels;
        size_t expected_size;
        uint8_t stream_count;
        uint8_t coupled_count;
        size_t i;
        if (packet[18u] == 1u && packet[9u] > 8u)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (packet[18u] == 2u &&
            !audio_ogg_opus_is_ambisonic_channel_count(packet[9u])) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if ((packet[18u] == 2u || packet[18u] == 255u) &&
            packet[9u] > QW_AUDIO_OGG_OPUS_MAX_CHANNELS) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        expected_size = AUDIO_OGG_OPUS_MAPPING_HEAD_SIZE + packet[9u];
        if (packet_size < expected_size) return QW_STATUS_ERR_FORMAT;
        if (packet_size != expected_size) return QW_STATUS_ERR_UNSUPPORTED;
        stream_count = packet[AUDIO_OGG_OPUS_HEAD_SIZE];
        coupled_count = packet[AUDIO_OGG_OPUS_HEAD_SIZE + 1u];
        decoded_channels = (uint16_t)stream_count + coupled_count;
        if (!stream_count || coupled_count > stream_count ||
            decoded_channels > UINT8_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        for (i = 0u; i < packet[9u]; ++i) {
            uint8_t mapped_channel =
                packet[AUDIO_OGG_OPUS_HEAD_SIZE + 2u + i];
            if (mapped_channel != UINT8_MAX &&
                mapped_channel >= decoded_channels) {
                return QW_STATUS_ERR_FORMAT;
            }
        }
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->channels = packet[9u];
    source->decoder_channels = packet[9u];
    source->pre_skip = qw_read_u16_le(packet + 10u);
    source->mapping_family = packet[18u];
    source->opus_head_size = packet_size;
    if (packet_size > sizeof(source->opus_head)) return QW_STATUS_ERR_FORMAT;
    memcpy(source->opus_head, packet, packet_size);
    return QW_STATUS_OK;
}
