#ifndef QUIREWISE_AUDIO_MATROSKA_ACM_PARSER_H
#define QUIREWISE_AUDIO_MATROSKA_ACM_PARSER_H

#include "audio_matroska_parser.h"

typedef struct QwAudioMatroskaAcmFormat {
    QwAudioMatroskaCodec codec;
    QwAudioWaveMsFormat wave_ms_format;
    uint64_t codec_delay_ns;
    uint32_t sample_rate;
    uint32_t mpeg1_bitrate;
    uint32_t channel_mask;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    int is_mpeg1_acm_format;
} QwAudioMatroskaAcmFormat;

/* Parses WAVEFORMATEX CodecPrivate data used by Matroska A_MS/ACM tracks. */
int qw_audio_matroska_acm_parse(const uint8_t *bytes,
                                size_t size,
                                QwAudioMatroskaAcmFormat *format,
                                QwConvertReport *report);

#endif
