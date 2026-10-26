#ifndef QUIREWISE_AUDIO_CAF_QDESIGN_PARSER_H
#define QUIREWISE_AUDIO_CAF_QDESIGN_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioCafQdesignCodec {
    QW_AUDIO_CAF_QDESIGN_QDMC = 1,
    QW_AUDIO_CAF_QDESIGN_QDM2 = 2
} QwAudioCafQdesignCodec;

typedef struct QwAudioCafQdesignSource {
    const uint8_t *data;
    size_t data_size;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    QwAudioCafQdesignCodec codec;
    const uint8_t *extradata;
    size_t extradata_size;
    int has_metadata;
} QwAudioCafQdesignSource;

typedef struct QwAudioCafQdesignFrame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioCafQdesignFrame;

int qw_audio_caf_qdesign_parse(const uint8_t *bytes,
                               size_t size,
                               QwAudioCafQdesignSource *source,
                               QwConvertReport *report);
int qw_audio_caf_qdesign_next_frame(const QwAudioCafQdesignSource *source,
                                    size_t packet_index,
                                    QwAudioCafQdesignFrame *frame);

#endif
