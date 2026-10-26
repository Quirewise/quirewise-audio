#ifndef QUIREWISE_AUDIO_AU_ADPCM_PARSER_H
#define QUIREWISE_AUDIO_AU_ADPCM_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioAuAdpcmCodec {
    QW_AUDIO_AU_ADPCM_CODEC_G726_LE = 1,
    QW_AUDIO_AU_ADPCM_CODEC_G722
} QwAudioAuAdpcmCodec;

typedef struct QwAudioAuAdpcmInfo {
    QwAudioAuAdpcmCodec codec;
    uint8_t bits_per_sample;
    uint8_t packet_alignment;
    uint8_t decoder_block_align;
} QwAudioAuAdpcmInfo;

int qw_audio_au_adpcm_describe(uint32_t encoding,
                               QwAudioAuAdpcmInfo *info);
int qw_audio_au_adpcm_sample_count(uint32_t encoding,
                                   size_t data_size,
                                   uint16_t channels,
                                   uint64_t *sample_count);
int qw_audio_au_adpcm_packet_sample_count(const QwAudioAuAdpcmInfo *info,
                                          size_t data_size,
                                          uint32_t *sample_count);

#endif
