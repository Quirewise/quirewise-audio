#ifndef QUIREWISE_AUDIO_AAC_SPECIFIC_CONFIG_PARSER_H
#define QUIREWISE_AUDIO_AAC_SPECIFIC_CONFIG_PARSER_H

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_AAC_SPECIFIC_CONFIG_MAX_SIZE = 64u };

typedef struct QwAudioAacSpecificConfig {
    uint32_t core_sample_rate;
    uint32_t output_sample_rate;
    uint32_t core_frames_per_packet;
    uint32_t output_frames_per_packet;
    uint16_t core_channels;
    uint16_t output_channels;
    uint8_t object_type;
    uint8_t channel_configuration;
    uint8_t sbr_signaled;
    uint8_t sbr_present;
    uint8_t ps_present;
} QwAudioAacSpecificConfig;

int qw_audio_aac_specific_config_parse(
    const uint8_t *config,
    size_t config_size,
    QwAudioAacSpecificConfig *parsed);

#endif
