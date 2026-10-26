#ifndef QUIREWISE_AUDIO_CAF_GSM_PARSER_H
#define QUIREWISE_AUDIO_CAF_GSM_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafGsmSource {
    const uint8_t *data;
    size_t data_size;
    size_t packet_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    int has_metadata;
} QwAudioCafGsmSource;

typedef struct QwAudioCafGsmFrame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioCafGsmFrame;

int qw_audio_caf_gsm_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafGsmSource *source,
                           QwConvertReport *report);
int qw_audio_caf_gsm_next_frame(const QwAudioCafGsmSource *source,
                                size_t packet_index,
                                QwAudioCafGsmFrame *frame);

#endif
