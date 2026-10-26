#ifndef QUIREWISE_AUDIO_DOLBY_DIGITAL_INTERNAL_H
#define QUIREWISE_AUDIO_DOLBY_DIGITAL_INTERNAL_H

#include "audio_dolby_digital_parser.h"

typedef struct QwAudioDolbyDigitalFrameHeader {
    uint32_t frame_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint16_t channels;
    QwAudioDolbyDigitalCodec codec;
    uint8_t frame_type;
    uint8_t substream_id;
} QwAudioDolbyDigitalFrameHeader;

int qw_audio_dolby_digital_read_bits(const uint8_t *bytes,
                                     size_t size,
                                     size_t *bit_position,
                                     unsigned bit_count,
                                     uint32_t *value);
int qw_audio_dolby_digital_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    QwAudioDolbyDigitalFrameHeader *header);
uint32_t qw_audio_dolby_digital_sample_rate(uint32_t sample_rate_code);
uint16_t qw_audio_dolby_digital_channel_count(uint32_t channel_mode,
                                              uint32_t lfe_on);

int qw_audio_ac3_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    uint32_t bitstream_id,
    QwAudioDolbyDigitalFrameHeader *header);
int qw_audio_eac3_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    uint32_t bitstream_id,
    QwAudioDolbyDigitalFrameHeader *header);

#endif
