#ifndef QUIREWISE_AUDIO_WAVE_GSM_PARSER_H
#define QUIREWISE_AUDIO_WAVE_GSM_PARSER_H

#include "audio_wave_chunks.h"

typedef struct QwAudioWaveGsmSource {
    const uint8_t *data;
    size_t data_size;
    size_t packet_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t block_align;
    uint16_t samples_per_block;
    uint16_t channels;
    int has_metadata;
} QwAudioWaveGsmSource;

typedef struct QwAudioWaveGsmFrame {
    const uint8_t *data;
    size_t size;
    size_t next_packet_index;
    uint32_t sample_count;
} QwAudioWaveGsmFrame;

int qw_audio_wave_gsm_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioWaveGsmSource *source,
                            QwConvertReport *report);
int qw_audio_wave_gsm_parse_chunks(const QwAudioWaveChunks *chunks,
                                   QwAudioWaveGsmSource *source,
                                   QwConvertReport *report);
int qw_audio_wave_gsm_next_frame(const QwAudioWaveGsmSource *source,
                                 size_t packet_index,
                                 QwAudioWaveGsmFrame *frame);

#endif
