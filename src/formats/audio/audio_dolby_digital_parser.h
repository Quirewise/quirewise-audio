#ifndef QUIREWISE_AUDIO_DOLBY_DIGITAL_PARSER_H
#define QUIREWISE_AUDIO_DOLBY_DIGITAL_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioDolbyDigitalCodec {
    QW_AUDIO_DOLBY_DIGITAL_CODEC_AC3 = 1,
    QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3 = 2
} QwAudioDolbyDigitalCodec;

typedef struct QwAudioDolbyDigitalSource {
    const uint8_t *data;
    size_t size;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint16_t channels;
    QwAudioDolbyDigitalCodec codec;
    uint8_t substream_id;
} QwAudioDolbyDigitalSource;

typedef struct QwAudioDolbyDigitalFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
} QwAudioDolbyDigitalFrame;

int qw_audio_dolby_digital_probe(const uint8_t *bytes, size_t size);
int qw_audio_dolby_digital_parse(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioDolbyDigitalSource *source,
                                 QwConvertReport *report);
int qw_audio_dolby_digital_next_frame(
    const QwAudioDolbyDigitalSource *source,
    size_t position,
    QwAudioDolbyDigitalFrame *frame);

#endif
