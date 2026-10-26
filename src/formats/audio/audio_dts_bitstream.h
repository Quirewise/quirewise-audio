#ifndef QUIREWISE_AUDIO_DTS_BITSTREAM_H
#define QUIREWISE_AUDIO_DTS_BITSTREAM_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioDtsBitReader {
    const uint8_t *data;
    size_t size;
    size_t bit_position;
    int little_endian;
    int packed_14bit;
} QwAudioDtsBitReader;

int qw_audio_dts_bit_reader_init(QwAudioDtsBitReader *reader,
                                 const uint8_t *data,
                                 size_t size,
                                 int little_endian,
                                 int packed_14bit);
int qw_audio_dts_bit_reader_read(QwAudioDtsBitReader *reader,
                                 unsigned bit_count,
                                 uint32_t *value);

#endif
