#ifndef QUIREWISE_AUDIO_OGG_OPUS_HEAD_PARSER_H
#define QUIREWISE_AUDIO_OGG_OPUS_HEAD_PARSER_H

#include <stddef.h>
#include <stdint.h>

struct QwAudioOggOpusSource;

int qw_audio_ogg_opus_head_parse(const uint8_t *packet,
                                 size_t packet_size,
                                 struct QwAudioOggOpusSource *source);
int qw_audio_ogg_opus_tags_validate(const uint8_t *packet,
                                    size_t packet_size);

#endif
