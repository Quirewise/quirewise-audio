#ifndef QUIREWISE_AUDIO_TAK_PARSER_H
#define QUIREWISE_AUDIO_TAK_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioTakSource {
    const uint8_t *data;
    size_t size;
    size_t audio_data_offset;
    size_t audio_data_end;
    const uint8_t *streaminfo;
    size_t streaminfo_size;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frame_samples;
    uint32_t channel_mask;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t codec_type;
    int has_metadata;
} QwAudioTakSource;

typedef struct QwAudioTakFrame {
    const uint8_t *data;
    size_t size;
    size_t next_position;
    uint32_t sample_count;
    uint32_t frame_number;
    int is_last;
} QwAudioTakFrame;

int qw_audio_tak_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioTakSource *source);

/* Returns 1 with a complete raw TAK frame, 0 at end, or a PAGE error. */
int qw_audio_tak_next_frame(const QwAudioTakSource *source,
                            size_t position,
                            QwAudioTakFrame *frame);

#endif
