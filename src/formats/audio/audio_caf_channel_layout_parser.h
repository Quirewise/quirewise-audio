#ifndef QUIREWISE_AUDIO_CAF_CHANNEL_LAYOUT_PARSER_H
#define QUIREWISE_AUDIO_CAF_CHANNEL_LAYOUT_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafChannelLayout {
    uint32_t channel_mask;
    int channel_order_unspecified;
} QwAudioCafChannelLayout;

int qw_audio_caf_channel_layout_parse(const uint8_t *bytes,
                                      size_t size,
                                      uint16_t channels,
                                      QwAudioCafChannelLayout *layout);

#endif
