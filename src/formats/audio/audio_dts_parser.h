#ifndef QUIREWISE_AUDIO_DTS_PARSER_H
#define QUIREWISE_AUDIO_DTS_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioDtsSource {
    const uint8_t *data;
    size_t size;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    int little_endian;
    int packed_14bit;
} QwAudioDtsSource;

typedef struct QwAudioDtsFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
} QwAudioDtsFrame;

int qw_audio_dts_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDtsSource *source,
                       QwConvertReport *report);
int qw_audio_dts_next_frame(const QwAudioDtsSource *source,
                            size_t position,
                            QwAudioDtsFrame *frame);

#endif
