#ifndef QUIREWISE_AUDIO_CAF_AAC_PARSER_H
#define QUIREWISE_AUDIO_CAF_AAC_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafAacSource {
    const uint8_t *data;
    size_t data_size;
    const uint8_t *packet_size_table;
    size_t packet_size_table_size;
    const uint8_t *audio_specific_config;
    size_t audio_specific_config_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frames_per_packet;
    uint32_t priming_frames;
    uint32_t remainder_frames;
    uint16_t channels;
    uint16_t object_type;
    uint8_t channel_configuration;
    uint8_t ps_present;
    int has_metadata;
} QwAudioCafAacSource;

typedef struct QwAudioCafAacFrame {
    const uint8_t *data;
    size_t size;
    size_t next_data_offset;
    size_t next_packet_size_offset;
    uint32_t sample_count;
} QwAudioCafAacFrame;

int qw_audio_caf_aac_parse(const uint8_t *bytes,
                           size_t size,
                           QwAudioCafAacSource *source,
                           QwConvertReport *report);
int qw_audio_caf_aac_next_frame(const QwAudioCafAacSource *source,
                                size_t packet_index,
                                size_t data_offset,
                                size_t packet_size_offset,
                                QwAudioCafAacFrame *frame);

#endif
