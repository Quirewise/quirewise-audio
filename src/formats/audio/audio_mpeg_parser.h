#ifndef QUIREWISE_AUDIO_MPEG_PARSER_H
#define QUIREWISE_AUDIO_MPEG_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMpegSource {
    const uint8_t *data;
    size_t size;
    size_t frame_count;
    size_t first_frame_offset;
    size_t frame_data_end;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t bitrate;
    uint16_t channels;
    uint8_t layer;
    uint8_t version_id;
    int has_metadata;
} QwAudioMpegSource;

typedef struct QwAudioMpegFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
    uint32_t bitrate;
} QwAudioMpegFrame;

int qw_audio_mpeg_probe(const uint8_t *bytes,
                        size_t size,
                        QwInputFormat input_format);
int qw_audio_mpeg_parse(const uint8_t *bytes,
                        size_t size,
                        QwInputFormat input_format,
                        QwAudioMpegSource *source,
                        QwConvertReport *report);
/* Parses a bounded sequence of raw MPEG audio frames without stream tags. */
int qw_audio_mpeg_parse_frames(const uint8_t *bytes,
                               size_t size,
                               QwInputFormat input_format,
                               QwAudioMpegSource *source,
                               QwConvertReport *report);
/* Parses a bounded sequence of raw MPEG audio frames of the requested layer. */
int qw_audio_mpeg_parse_layer_frames(const uint8_t *bytes,
                                     size_t size,
                                     uint8_t expected_layer,
                                     QwAudioMpegSource *source,
                                     QwConvertReport *report);
int qw_audio_mpeg_next_frame(const QwAudioMpegSource *source,
                             size_t position,
                             QwAudioMpegFrame *frame);

#endif
