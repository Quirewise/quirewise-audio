#ifndef QUIREWISE_AUDIO_AAC_PCE_PARSER_H
#define QUIREWISE_AUDIO_AAC_PCE_PARSER_H

#include <stddef.h>
#include <stdint.h>

#define QW_AUDIO_AAC_PCE_ANY_OBJECT_TYPE UINT8_MAX

int qw_audio_aac_pce_parse_syntax_at_bit_offset(
    const uint8_t *data,
    size_t size,
    size_t bit_offset,
    uint8_t expected_object_type,
    uint8_t expected_sampling_frequency_index,
    uint16_t *channel_count,
    size_t *next_bit_offset);

int qw_audio_aac_pce_parse_first_raw_data_block(
    const uint8_t *data,
    size_t size,
    uint8_t expected_object_type,
    uint8_t expected_sampling_frequency_index,
    uint16_t *channel_count);

#endif
