#include "audio_dts_bitstream.h"

#include "quirewise/status.h"

#include <string.h>

int qw_audio_dts_bit_reader_init(QwAudioDtsBitReader *reader,
                                 const uint8_t *data,
                                 size_t size,
                                 int little_endian,
                                 int packed_14bit)
{
    if (!reader || !data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(reader, 0, sizeof(*reader));
    if (packed_14bit && ((size & 1u) != 0u || size / 2u > SIZE_MAX / 14u))
        return QW_STATUS_ERR_FORMAT;
    if (!packed_14bit && size > SIZE_MAX / 8u) return QW_STATUS_ERR_FORMAT;
    reader->data = data;
    reader->size = size;
    reader->little_endian = little_endian != 0;
    reader->packed_14bit = packed_14bit != 0;
    return QW_STATUS_OK;
}

int qw_audio_dts_bit_reader_read(QwAudioDtsBitReader *reader,
                                 unsigned bit_count,
                                 uint32_t *value)
{
    size_t total_bits;
    uint32_t result = 0u;
    unsigned i;
    if (!reader || !reader->data || !value || bit_count > 32u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    total_bits = reader->packed_14bit
        ? (reader->size / 2u) * 14u
        : reader->size * 8u;
    if (reader->bit_position > total_bits ||
        bit_count > total_bits - reader->bit_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    for (i = 0u; i < bit_count; ++i) {
        size_t position = reader->bit_position++;
        if (reader->packed_14bit) {
            size_t word_index = position / 14u;
            unsigned bit_in_word = (unsigned)(position % 14u);
            size_t byte_index = word_index * 2u;
            uint16_t word;
            if (byte_index > reader->size - 2u) return QW_STATUS_ERR_FORMAT;
            if (reader->little_endian) {
                word = (uint16_t)(((uint16_t)reader->data[byte_index + 1u] << 8u) |
                                  reader->data[byte_index]);
            } else {
                word = (uint16_t)(((uint16_t)reader->data[byte_index] << 8u) |
                                  reader->data[byte_index + 1u]);
            }
            result = (result << 1u) |
                     ((word >> (13u - bit_in_word)) & 1u);
        } else {
            size_t byte_index = position / 8u;
            if (reader->little_endian) byte_index ^= 1u;
            if (byte_index >= reader->size) return QW_STATUS_ERR_FORMAT;
            result = (result << 1u) |
                     ((reader->data[byte_index] >> (7u - (position % 8u))) & 1u);
        }
    }
    *value = result;
    return QW_STATUS_OK;
}
