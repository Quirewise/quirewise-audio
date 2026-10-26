#include "audio_aac_pce_parser.h"

#include "quirewise/status.h"

typedef struct QwAudioAacPceBitReader {
    const uint8_t *data;
    size_t bit_count;
    size_t bit_position;
} QwAudioAacPceBitReader;

static int aac_pce_read_bits(QwAudioAacPceBitReader *reader,
                             unsigned count,
                             uint32_t *value)
{
    uint32_t result = 0u;
    unsigned index;

    if (!reader || !value || count > 32u ||
        reader->bit_position > reader->bit_count ||
        (size_t)count > reader->bit_count - reader->bit_position) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        size_t position = reader->bit_position++;
        result = (result << 1u) |
            ((reader->data[position / 8u] >> (7u - position % 8u)) & 1u);
    }
    *value = result;
    return 1;
}

static int aac_pce_skip_bits(QwAudioAacPceBitReader *reader, size_t count)
{
    uint32_t ignored;

    while (count >= 32u) {
        if (!aac_pce_read_bits(reader, 32u, &ignored)) return 0;
        count -= 32u;
    }
    return count == 0u || aac_pce_read_bits(reader, (unsigned)count, &ignored);
}

static int aac_pce_read_channel_elements(QwAudioAacPceBitReader *reader,
                                         uint32_t element_count,
                                         uint32_t *channel_count)
{
    uint32_t index;

    for (index = 0u; index < element_count; ++index) {
        uint32_t is_cpe;
        uint32_t tag;
        if (!aac_pce_read_bits(reader, 1u, &is_cpe) ||
            !aac_pce_read_bits(reader, 4u, &tag)) {
            return 0;
        }
        (void)tag;
        *channel_count += is_cpe ? 2u : 1u;
    }
    return 1;
}

int qw_audio_aac_pce_parse_syntax_at_bit_offset(
    const uint8_t *data,
    size_t size,
    size_t bit_offset,
    uint8_t expected_object_type,
    uint8_t expected_sampling_frequency_index,
    uint16_t *channel_count,
    size_t *next_bit_offset)
{
    QwAudioAacPceBitReader reader;
    uint32_t element_instance_tag;
    uint32_t object_type;
    uint32_t sampling_frequency_index;
    uint32_t num_front;
    uint32_t num_side;
    uint32_t num_back;
    uint32_t num_lfe;
    uint32_t num_assoc_data;
    uint32_t num_valid_cc;
    uint32_t mixdown_present;
    uint32_t channel_total = 0u;
    uint32_t comment_bytes;
    size_t comment_bits;
    size_t aligned_position;

    if (!data || !size || !channel_count || size > SIZE_MAX / 8u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    *channel_count = 0u;
    reader.data = data;
    reader.bit_count = size * 8u;
    if (bit_offset > reader.bit_count) return QW_STATUS_ERR_FORMAT;
    reader.bit_position = bit_offset;
    if (!aac_pce_read_bits(&reader, 4u, &element_instance_tag) ||
        !aac_pce_read_bits(&reader, 2u, &object_type) ||
        !aac_pce_read_bits(&reader, 4u, &sampling_frequency_index) ||
        !aac_pce_read_bits(&reader, 4u, &num_front) ||
        !aac_pce_read_bits(&reader, 4u, &num_side) ||
        !aac_pce_read_bits(&reader, 4u, &num_back) ||
        !aac_pce_read_bits(&reader, 2u, &num_lfe) ||
        !aac_pce_read_bits(&reader, 3u, &num_assoc_data) ||
        !aac_pce_read_bits(&reader, 4u, &num_valid_cc)) {
        return QW_STATUS_ERR_FORMAT;
    }
    (void)element_instance_tag;
    if ((expected_object_type != QW_AUDIO_AAC_PCE_ANY_OBJECT_TYPE &&
         object_type != expected_object_type) ||
        sampling_frequency_index != expected_sampling_frequency_index) {
        return QW_STATUS_ERR_FORMAT;
    }

    if (!aac_pce_read_bits(&reader, 1u, &mixdown_present))
        return QW_STATUS_ERR_FORMAT;
    if (mixdown_present && !aac_pce_skip_bits(&reader, 4u))
        return QW_STATUS_ERR_FORMAT;
    if (!aac_pce_read_bits(&reader, 1u, &mixdown_present))
        return QW_STATUS_ERR_FORMAT;
    if (mixdown_present && !aac_pce_skip_bits(&reader, 4u))
        return QW_STATUS_ERR_FORMAT;
    if (!aac_pce_read_bits(&reader, 1u, &mixdown_present))
        return QW_STATUS_ERR_FORMAT;
    if (mixdown_present && !aac_pce_skip_bits(&reader, 3u))
        return QW_STATUS_ERR_FORMAT;

    if (!aac_pce_read_channel_elements(&reader, num_front, &channel_total) ||
        !aac_pce_read_channel_elements(&reader, num_side, &channel_total) ||
        !aac_pce_read_channel_elements(&reader, num_back, &channel_total) ||
        !aac_pce_skip_bits(&reader, (size_t)num_lfe * 4u) ||
        !aac_pce_skip_bits(&reader, (size_t)num_assoc_data * 4u) ||
        !aac_pce_skip_bits(&reader, (size_t)num_valid_cc * 5u)) {
        return QW_STATUS_ERR_FORMAT;
    }
    channel_total += num_lfe;
    if (!channel_total || channel_total > UINT16_MAX) return QW_STATUS_ERR_UNSUPPORTED;

    aligned_position = (reader.bit_position + 7u) & ~(size_t)7u;
    if (aligned_position > reader.bit_count) return QW_STATUS_ERR_FORMAT;
    reader.bit_position = aligned_position;
    if (!aac_pce_read_bits(&reader, 8u, &comment_bytes))
        return QW_STATUS_ERR_FORMAT;
    comment_bits = (size_t)comment_bytes * 8u;
    if (!aac_pce_skip_bits(&reader, comment_bits)) return QW_STATUS_ERR_FORMAT;

    *channel_count = (uint16_t)channel_total;
    if (next_bit_offset) *next_bit_offset = reader.bit_position;
    return QW_STATUS_OK;
}

int qw_audio_aac_pce_parse_first_raw_data_block(
    const uint8_t *data,
    size_t size,
    uint8_t expected_object_type,
    uint8_t expected_sampling_frequency_index,
    uint16_t *channel_count)
{
    QwAudioAacPceBitReader reader;
    uint32_t id_syn_ele;

    if (!data || !size || !channel_count || size > SIZE_MAX / 8u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    reader.data = data;
    reader.bit_count = size * 8u;
    reader.bit_position = 0u;
    if (!aac_pce_read_bits(&reader, 3u, &id_syn_ele))
        return QW_STATUS_ERR_FORMAT;
    if (id_syn_ele != 5u) return QW_STATUS_ERR_UNSUPPORTED;
    return qw_audio_aac_pce_parse_syntax_at_bit_offset(
        data, size, reader.bit_position, expected_object_type,
        expected_sampling_frequency_index, channel_count, NULL);
}
