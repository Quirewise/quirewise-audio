#ifndef QUIREWISE_AUDIO_VOC_ADPCM_PARSER_H
#define QUIREWISE_AUDIO_VOC_ADPCM_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioVocAdpcmMode {
    QW_AUDIO_VOC_ADPCM_MODE_NONE = 0,
    QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4,
    QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3,
    QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2,
    QW_AUDIO_VOC_ADPCM_MODE_CT_4
} QwAudioVocAdpcmMode;

int qw_audio_voc_adpcm_mode_from_packing_method(
    uint8_t packing_method,
    QwAudioVocAdpcmMode *mode);
int qw_audio_voc_adpcm_mode_from_codec_tag(uint16_t codec_tag,
                                           uint16_t bits_per_sample,
                                           QwAudioVocAdpcmMode *mode);
uint16_t qw_audio_voc_adpcm_mode_bits_per_sample(QwAudioVocAdpcmMode mode);
int qw_audio_voc_adpcm_mode_supports_channels(QwAudioVocAdpcmMode mode,
                                             uint16_t channels);
int qw_audio_voc_adpcm_sample_count(QwAudioVocAdpcmMode mode,
                                    size_t data_size,
                                    uint16_t channels,
                                    int has_initial_sample,
                                    uint32_t *sample_count);

#endif
