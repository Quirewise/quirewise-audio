#ifndef QUIREWISE_AUDIO_CAF_MACE_PARSER_H
#define QUIREWISE_AUDIO_CAF_MACE_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioCafMaceCodec {
    QW_AUDIO_CAF_MACE3 = 1,
    QW_AUDIO_CAF_MACE6 = 2
} QwAudioCafMaceCodec;

typedef struct QwAudioCafMaceSource {
    const uint8_t *data;
    size_t data_size;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    QwAudioCafMaceCodec codec;
    int has_metadata;
} QwAudioCafMaceSource;

typedef struct QwAudioCafMaceFrame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioCafMaceFrame;

int qw_audio_caf_mace_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioCafMaceSource *source,
                            QwConvertReport *report);
int qw_audio_caf_mace_next_frame(const QwAudioCafMaceSource *source,
                                 size_t packet_index,
                                 QwAudioCafMaceFrame *frame);

#endif
