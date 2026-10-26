#include "audio_wavpack_matroska_parser.h"

#include "audio_wavpack_internal.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

static void audio_wavpack_write_u32_le(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
    bytes[2] = (uint8_t)(value >> 16u);
    bytes[3] = (uint8_t)(value >> 24u);
}

static void audio_wavpack_write_u16_le(uint8_t *bytes, uint16_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8u);
}

int qw_audio_wavpack_matroska_packet_reconstruct(
        const uint8_t *bytes,
        size_t size,
        uint16_t version,
        uint32_t sample_rate,
        uint16_t channels,
        uint8_t *output,
        size_t output_capacity,
        size_t *output_size)
{
    QwAudioWavpackMatroskaPacket packet;
    size_t required_size;
    size_t index;
    int rc;

    if (!output_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *output_size = 0u;
    rc = qw_audio_wavpack_matroska_packet_parse(
        bytes, size, version, sample_rate, channels, &packet);
    if (rc != QW_STATUS_OK) return rc;
    required_size = 0u;
    for (index = 0u; index < packet.block_count; ++index) {
        size_t block_size = packet.blocks[index].compressed_data_size;
        if (block_size > UINT32_MAX - 24u || block_size > SIZE_MAX - 32u ||
            required_size > SIZE_MAX - (block_size + 32u)) {
            return QW_STATUS_ERR_FORMAT;
        }
        required_size += block_size + 32u;
    }
    if (required_size > INT_MAX) return QW_STATUS_ERR_UNSUPPORTED;
    *output_size = required_size;
    if (!output) {
        return output_capacity ? QW_STATUS_ERR_INVALID_ARGUMENT : QW_STATUS_OK;
    }
    if (output_capacity < required_size) return QW_STATUS_ERR_INVALID_ARGUMENT;

    required_size = 0u;
    for (index = 0u; index < packet.block_count; ++index) {
        const QwAudioWavpackMatroskaBlock *block = &packet.blocks[index];
        uint8_t *header = output + required_size;
        memcpy(header, "wvpk", 4u);
        audio_wavpack_write_u32_le(
            header + 4u, (uint32_t)(block->compressed_data_size + 24u));
        audio_wavpack_write_u16_le(header + 8u, version);
        audio_wavpack_write_u16_le(header + 10u, 0u);
        audio_wavpack_write_u32_le(header + 12u, 0u);
        audio_wavpack_write_u32_le(header + 16u, 0u);
        audio_wavpack_write_u32_le(header + 20u, packet.block_samples);
        audio_wavpack_write_u32_le(header + 24u, block->flags);
        audio_wavpack_write_u32_le(header + 28u, block->crc);
        memcpy(header + 32u, block->compressed_data,
               block->compressed_data_size);
        required_size += block->compressed_data_size + 32u;
    }
    return QW_STATUS_OK;
}

int qw_audio_wavpack_matroska_packet_parse(
        const uint8_t *bytes,
        size_t size,
        uint16_t version,
        uint32_t sample_rate,
        uint16_t channels,
        QwAudioWavpackMatroskaPacket *packet)
{
    size_t position;
    uint16_t channel_sum = 0u;
    int hybrid_mode = -1;
    int dsd_mode = -1;
    int final_seen = 0;
    uint32_t sample_rate_index;
    if (!bytes || !packet || !sample_rate)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    *packet = (QwAudioWavpackMatroskaPacket){0};
    if (!channels || channels > QW_AUDIO_WAVPACK_MATROSKA_MAX_BLOCKS)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (size < 13u) return QW_STATUS_ERR_FORMAT;
    if (version < 0x0402u || version > 0x0410u)
        return QW_STATUS_ERR_UNSUPPORTED;
    packet->block_samples = qw_read_u32_le(bytes);
    if (!packet->block_samples) return QW_STATUS_ERR_FORMAT;

    position = 4u;
    while (position < size) {
        QwAudioWavpackMatroskaBlock *block;
        size_t block_size;
        uint32_t flags;
        uint32_t block_sample_rate;
        uint16_t block_channels;
        int single_block;
        int block_is_dsd;

        if (size - position < 8u) return QW_STATUS_ERR_FORMAT;
        flags = qw_read_u32_le(bytes + position);
        position += 4u;
        block_is_dsd = (flags & AUDIO_WAVPACK_DSD_FLAG) != 0u;
        if (dsd_mode >= 0 && block_is_dsd != dsd_mode)
            return QW_STATUS_ERR_FORMAT;
        dsd_mode = block_is_dsd;
        if (block_is_dsd && (flags & AUDIO_WAVPACK_HYBRID_FLAG))
            return QW_STATUS_ERR_UNSUPPORTED;
        if (hybrid_mode >= 0 &&
            ((flags & AUDIO_WAVPACK_HYBRID_FLAG) != 0u) != hybrid_mode) {
            return QW_STATUS_ERR_FORMAT;
        }
        hybrid_mode = (flags & AUDIO_WAVPACK_HYBRID_FLAG) != 0u;
        packet->is_hybrid = hybrid_mode;
        packet->is_dsd = dsd_mode;
        if ((flags & AUDIO_WAVPACK_FALSE_STEREO_FLAG) &&
            (flags & AUDIO_WAVPACK_MONO_FLAG)) {
            return QW_STATUS_ERR_FORMAT;
        }
        sample_rate_index = (flags >> 23u) & 0x0fu;

        if (packet->block_count >= QW_AUDIO_WAVPACK_MATROSKA_MAX_BLOCKS)
            return QW_STATUS_ERR_UNSUPPORTED;
        block = &packet->blocks[packet->block_count];
        block->flags = flags;
        block->crc = qw_read_u32_le(bytes + position);
        position += 4u;
        single_block =
            (flags & (AUDIO_WAVPACK_INITIAL_FLAG | AUDIO_WAVPACK_FINAL_FLAG)) ==
            (AUDIO_WAVPACK_INITIAL_FLAG | AUDIO_WAVPACK_FINAL_FLAG);
        if (single_block) {
            if (packet->block_count || final_seen)
                return QW_STATUS_ERR_FORMAT;
            block_size = size - position;
        } else {
            if (size - position < 4u) return QW_STATUS_ERR_FORMAT;
            block_size = (size_t)qw_read_u32_le(bytes + position);
            position += 4u;
        }
        if (!block_size || (size_t)block_size > size - position)
            return QW_STATUS_ERR_FORMAT;

        if (packet->block_count == 0u) {
            if ((flags & AUDIO_WAVPACK_INITIAL_FLAG) == 0u)
                return QW_STATUS_ERR_UNSUPPORTED;
        } else if ((flags & AUDIO_WAVPACK_INITIAL_FLAG) != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (flags & AUDIO_WAVPACK_FINAL_FLAG) {
            final_seen = 1;
            if ((size_t)block_size != size - position)
                return QW_STATUS_ERR_FORMAT;
        }

        block_channels = (flags & AUDIO_WAVPACK_FALSE_STEREO_FLAG)
            ? 2u : (flags & AUDIO_WAVPACK_MONO_FLAG) ? 1u : 2u;
        if (block_channels > channels - channel_sum)
            return QW_STATUS_ERR_FORMAT;
        channel_sum = (uint16_t)(channel_sum + block_channels);
        block->compressed_data = bytes + position;
        block->compressed_data_size = block_size;
        if (sample_rate_index == 15u) {
            int rc = audio_wavpack_parse_custom_sample_rate(
                block->compressed_data, block->compressed_data_size,
                &block_sample_rate);
            if (rc != QW_STATUS_OK) return rc;
        } else if (!audio_wavpack_sample_rates[sample_rate_index]) {
            return QW_STATUS_ERR_UNSUPPORTED;
        } else {
            block_sample_rate = audio_wavpack_sample_rates[sample_rate_index];
        }
        if (block_is_dsd) {
            uint32_t rate_factor;
            int rc = audio_wavpack_parse_dsd_rate_factor(
                block->compressed_data, block->compressed_data_size,
                &rate_factor);
            if (rc != QW_STATUS_OK) return rc;
            if (rate_factor > (uint32_t)INT_MAX / block_sample_rate)
                return QW_STATUS_ERR_UNSUPPORTED;
            block_sample_rate *= rate_factor;
        }
        if (block_sample_rate != sample_rate) return QW_STATUS_ERR_FORMAT;
        ++packet->block_count;
        position += block_size;
        if (final_seen && position != size) return QW_STATUS_ERR_FORMAT;
    }
    if (!packet->block_count || !final_seen || channel_sum != channels)
        return QW_STATUS_ERR_UNSUPPORTED;
    return QW_STATUS_OK;
}
