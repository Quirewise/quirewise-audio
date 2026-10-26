#ifndef QUIREWISE_AUDIO_MP4_AAC_CONFIG_PARSER_H
#define QUIREWISE_AUDIO_MP4_AAC_CONFIG_PARSER_H

#include "audio_mp4_parser.h"

int qw_audio_mp4_parse_aac_config(const uint8_t *config,
                                  size_t config_size,
                                  uint32_t sample_rate,
                                  uint16_t entry_channels,
                                  QwAudioMp4Source *source);

#endif
