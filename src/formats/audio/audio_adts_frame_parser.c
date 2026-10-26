#include "audio_adts_frame_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum { ADTS_HEADER_SIZE = 7u };

static int adts_frame_frequency(uint8_t index, uint32_t *sample_rate)
{
    static const uint32_t sample_rates[] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u, 7350u
    };

    if (!sample_rate || index >= sizeof(sample_rates) / sizeof(sample_rates[0]))
        return 0;
    *sample_rate = sample_rates[index];
    return 1;
}

static uint16_t adts_frame_channels(uint8_t configuration)
{
    static const uint8_t channel_counts[] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u};

    if (configuration >= sizeof(channel_counts) / sizeof(channel_counts[0]))
        return 0u;
    return channel_counts[configuration];
}

static uint16_t adts_frame_read_u16_be(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

int qw_audio_adts_frame_parse_header(const uint8_t *data,
                                     size_t frame_data_end,
                                     size_t position,
                                     QwAudioAdtsFrame *frame)
{
    const uint8_t *header;
    size_t remaining;
    size_t header_size;
    size_t frame_size;
    size_t raw_data_block_count;
    size_t payload_size;
    size_t raw_data_block_positions[
        QW_AUDIO_ADTS_MAX_RAW_DATA_BLOCKS - 1u] = {0u};
    size_t index;
    uint8_t profile;
    uint8_t frequency_index;
    uint8_t channel_configuration;
    int protection_absent;
    uint32_t sample_rate;
    uint16_t channels;

    if (!data || !frame) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == frame_data_end) return 0;
    if (position > frame_data_end) return QW_STATUS_ERR_FORMAT;
    remaining = frame_data_end - position;
    if (remaining < ADTS_HEADER_SIZE) return QW_STATUS_ERR_FORMAT;
    header = data + position;
    if (header[0] != 0xffu || (header[1] & 0xf6u) != 0xf0u)
        return QW_STATUS_ERR_FORMAT;

    profile = (uint8_t)(((header[2] >> 6u) & 0x03u) + 1u);
    frequency_index = (uint8_t)((header[2] >> 2u) & 0x0fu);
    channel_configuration = (uint8_t)(((header[2] & 0x01u) << 2u) |
                                      ((header[3] >> 6u) & 0x03u));
    /* The two-bit count stores raw_data_blocks_in_frame minus one. */
    raw_data_block_count = (size_t)(header[6] & 0x03u) + 1u;
    protection_absent = (header[1] & 0x01u) != 0u;
    /* Multi-block ADTS needs the protected header's explicit position table. */
    if (profile > 3u || !adts_frame_frequency(frequency_index, &sample_rate) ||
        (raw_data_block_count > 1u && protection_absent)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    header_size = ADTS_HEADER_SIZE;
    if (!protection_absent) {
        header_size += (raw_data_block_count - 1u) * 2u + 2u;
    }
    if (remaining < header_size) return QW_STATUS_ERR_FORMAT;
    frame_size = ((size_t)(header[3] & 0x03u) << 11u) |
                 ((size_t)header[4] << 3u) |
                 ((size_t)header[5] >> 5u);
    if (frame_size <= header_size || frame_size > remaining)
        return QW_STATUS_ERR_FORMAT;

    payload_size = frame_size - header_size;
    if (raw_data_block_count > 1u) {
        /* Positions are byte offsets from block 0; a CRC word precedes each
         * next block, so the previous block's payload ends two bytes earlier. */
        for (index = 0u; index + 1u < raw_data_block_count; ++index) {
            size_t position_offset = ADTS_HEADER_SIZE + index * 2u;
            size_t relative_position =
                adts_frame_read_u16_be(header + position_offset);
            if (relative_position <= (index ? raw_data_block_positions[index - 1u]
                                            : 0u) ||
                relative_position >= payload_size) {
                return QW_STATUS_ERR_FORMAT;
            }
            raw_data_block_positions[index] = relative_position;
        }
    }

    channels = adts_frame_channels(channel_configuration);
    frame->raw_data_block_count = raw_data_block_count;
    if (raw_data_block_count == 1u) {
        frame->raw_data_blocks[0].data = header + header_size;
        frame->raw_data_blocks[0].size = payload_size;
    } else {
        const uint8_t *raw_data = header + header_size;
        for (index = 0u; index < raw_data_block_count; ++index) {
            size_t start = index == 0u
                ? 0u : raw_data_block_positions[index - 1u];
            size_t end = index + 1u < raw_data_block_count
                ? raw_data_block_positions[index] : payload_size;
            if (end <= start + 2u) return QW_STATUS_ERR_FORMAT;
            frame->raw_data_blocks[index].data = raw_data + start;
            frame->raw_data_blocks[index].size = end - start - 2u;
        }
    }
    frame->next_position = position + frame_size;
    frame->sample_count = (uint32_t)(1024u * raw_data_block_count);
    frame->sample_rate = sample_rate;
    frame->channels = channels;
    frame->audio_object_type = profile;
    frame->sampling_frequency_index = frequency_index;
    frame->channel_configuration = channel_configuration;
    return 1;
}
