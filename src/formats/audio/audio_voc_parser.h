#ifndef QUIREWISE_AUDIO_VOC_PARSER_H
#define QUIREWISE_AUDIO_VOC_PARSER_H

#include "quirewise/convert.h"
#include "audio_voc_repeat_parser.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES = 4096u };

typedef enum QwAudioVocCodec {
    QW_AUDIO_VOC_CODEC_PCM_U8 = 0,
    QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4 = 1,
    QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_3 = 2,
    QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2 = 3,
    QW_AUDIO_VOC_CODEC_PCM_S16LE = 4,
    QW_AUDIO_VOC_CODEC_PCM_ALAW = 6,
    QW_AUDIO_VOC_CODEC_PCM_MULAW = 7,
    QW_AUDIO_VOC_CODEC_ADPCM_CT = 0x0200
} QwAudioVocCodec;

typedef struct QwAudioVocSource {
    const uint8_t *data;
    size_t size;
    size_t data_start;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    QwAudioVocCodec codec;
    int has_metadata;
} QwAudioVocSource;

typedef struct QwAudioVocFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
    uint32_t silence_remaining;
    int is_silence;
} QwAudioVocFrame;

int qw_audio_voc_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioVocSource *source,
                       QwConvertReport *report);
int qw_audio_voc_next_frame(const QwAudioVocSource *source,
                            QwAudioVocCursor *cursor,
                            QwAudioVocFrame *frame);

#endif
