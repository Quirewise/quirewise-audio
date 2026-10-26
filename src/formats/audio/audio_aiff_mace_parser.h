#ifndef QUIREWISE_AUDIO_AIFF_MACE_PARSER_H
#define QUIREWISE_AUDIO_AIFF_MACE_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioAiffMaceCodec {
    QW_AUDIO_AIFF_MACE3 = 1,
    QW_AUDIO_AIFF_MACE6 = 2
} QwAudioAiffMaceCodec;

typedef struct QwAudioAiffMaceSource {
    const uint8_t *sample_data;
    size_t sample_bytes;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    QwAudioAiffMaceCodec codec;
    int has_metadata;
} QwAudioAiffMaceSource;

typedef struct QwAudioAiffMaceFrame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioAiffMaceFrame;

int qw_audio_aiff_mace_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffMaceSource *source,
                             QwConvertReport *report);
int qw_audio_aiff_mace_next_frame(const QwAudioAiffMaceSource *source,
                                  size_t packet_index,
                                  QwAudioAiffMaceFrame *frame);

#endif
