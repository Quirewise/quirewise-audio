#ifndef QUIREWISE_AUDIO_ADTS_PARSER_H
#define QUIREWISE_AUDIO_ADTS_PARSER_H

#include "quirewise/convert.h"
#include "audio_adts_frame_parser.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAdtsSource {
    const uint8_t *data;
    size_t size;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint8_t audio_object_type;
    uint8_t sampling_frequency_index;
    uint8_t channel_configuration;
    uint8_t audio_specific_config[2];
    int has_metadata;
} QwAudioAdtsSource;

int qw_audio_adts_parse(const uint8_t *bytes,
                        size_t size,
                        QwAudioAdtsSource *source,
                        QwConvertReport *report);
int qw_audio_adts_next_frame(const QwAudioAdtsSource *source,
                             size_t position,
                             QwAudioAdtsFrame *frame);

#endif
