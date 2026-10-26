#ifndef QUIREWISE_AUDIO_VOC_PARSER_INTERNAL_H
#define QUIREWISE_AUDIO_VOC_PARSER_INTERNAL_H

#include "audio_voc_adpcm_parser.h"
#include "audio_voc_parser.h"

enum {
    AUDIO_VOC_NEW_DATA_HEADER_SIZE = 12u,
    AUDIO_VOC_LEGACY_DATA_HEADER_SIZE = 2u
};

int qw_audio_voc_codec_adpcm_mode(QwAudioVocCodec codec,
                                  uint16_t bits_per_sample,
                                  QwAudioVocAdpcmMode *mode);
int qw_audio_voc_codec_is_adpcm(QwAudioVocCodec codec);
int qw_audio_voc_codec_has_adpcm_initial_sample(QwAudioVocCodec codec);

#endif
