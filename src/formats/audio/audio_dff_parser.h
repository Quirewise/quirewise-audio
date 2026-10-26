#ifndef QUIREWISE_AUDIO_DFF_PARSER_H
#define QUIREWISE_AUDIO_DFF_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_DFF_MAX_CHANNELS = 6u };

typedef enum QwAudioDffCodec {
    QW_AUDIO_DFF_CODEC_DSD = 1,
    QW_AUDIO_DFF_CODEC_DST = 2
} QwAudioDffCodec;

typedef struct QwAudioDffSource {
    const uint8_t *data;
    size_t size;
    size_t audio_data_offset;
    size_t audio_data_size;
    size_t audio_data_end;
    size_t frame_count;
    size_t frame_data_size;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t channel_ids[QW_AUDIO_DFF_MAX_CHANNELS];
    uint32_t channel_mask;
    uint32_t samples_per_frame;
    uint16_t channels;
    uint16_t frame_rate;
    QwAudioDffCodec codec;
    int has_metadata;
} QwAudioDffSource;

typedef struct QwAudioDffFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
} QwAudioDffFrame;

int qw_audio_dff_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDffSource *source,
                       QwConvertReport *report);

/* Returns 1 with a frame, 0 at end of the DST chunk, or a PAGE error. */
int qw_audio_dff_next_dst_frame(const QwAudioDffSource *source,
                                size_t position,
                                QwAudioDffFrame *frame);

#endif
