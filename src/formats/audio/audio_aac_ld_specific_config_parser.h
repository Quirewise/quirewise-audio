#ifndef QUIREWISE_AUDIO_AAC_LD_SPECIFIC_CONFIG_PARSER_H
#define QUIREWISE_AUDIO_AAC_LD_SPECIFIC_CONFIG_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioAacLdSpecificConfig {
    uint16_t channels;
    uint16_t frames_per_packet;
    uint8_t resilience_flags;
    uint8_t extension_flag3;
} QwAudioAacLdSpecificConfig;

int qw_audio_aac_ld_specific_config_parse_at_bit_offset(
    const uint8_t *config,
    size_t config_size,
    size_t bit_offset,
    uint32_t channel_configuration,
    uint32_t frequency_index,
    QwAudioAacLdSpecificConfig *parsed,
    size_t *next_bit_offset);

#endif
