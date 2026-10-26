#ifndef QUIREWISE_AUDIO_FLAC_PARSER_H
#define QUIREWISE_AUDIO_FLAC_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_FLAC_STREAMINFO_SIZE = 34u };

typedef struct QwAudioFlacSource {
    const uint8_t *data;
    size_t size;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint64_t stream_sample_count;
    uint32_t sample_rate;
    uint32_t min_block_size;
    uint32_t max_block_size;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t blocking_strategy;
    uint8_t streaminfo[QW_AUDIO_FLAC_STREAMINFO_SIZE];
} QwAudioFlacSource;

typedef struct QwAudioFlacFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
} QwAudioFlacFrame;

int qw_audio_flac_probe(const uint8_t *bytes, size_t size);
int qw_audio_flac_parse_streaminfo(const uint8_t *bytes,
                                   size_t size,
                                   QwAudioFlacSource *source);
int qw_audio_flac_parse(const uint8_t *bytes,
                        size_t size,
                        QwAudioFlacSource *source,
                        QwConvertReport *report);
int qw_audio_flac_next_frame(const QwAudioFlacSource *source,
                             size_t position,
                             QwAudioFlacFrame *frame);

#endif
