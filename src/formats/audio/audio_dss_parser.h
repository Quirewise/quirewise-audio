#ifndef QUIREWISE_AUDIO_DSS_PARSER_H
#define QUIREWISE_AUDIO_DSS_PARSER_H

#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_DSS_BLOCK_SIZE = 512u,
    QW_AUDIO_DSS_BLOCK_HEADER_SIZE = 6u,
    QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE =
        QW_AUDIO_DSS_BLOCK_SIZE - QW_AUDIO_DSS_BLOCK_HEADER_SIZE,
    QW_AUDIO_DSS_SP_PACKET_SIZE = 42u
};

typedef enum QwAudioDssCodec {
    QW_AUDIO_DSS_CODEC_SP = 1,
    QW_AUDIO_DSS_CODEC_G723_1 = 2
} QwAudioDssCodec;

typedef struct QwAudioDssSource {
    const uint8_t *data;
    size_t size;
    size_t audio_data_offset;
    size_t audio_data_size;
    size_t block_count;
    size_t payload_size;
    size_t frame_count;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t samples_per_frame;
    QwAudioDssCodec codec;
    uint16_t channels;
    int has_metadata;
} QwAudioDssSource;

typedef struct QwAudioDssFrame {
    size_t size;
    size_t next_position;
} QwAudioDssFrame;

int qw_audio_dss_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDssSource *source,
                       QwConvertReport *report);

/* Read the logical audio payload while skipping each six-byte block header. */
int qw_audio_dss_payload_read(const QwAudioDssSource *source,
                              size_t position,
                              uint8_t *output,
                              size_t size);

/* Returns 1 with a complete G.723.1 frame, 0 at EOF or on a short final frame,
 * or a PAGE error for malformed framing. */
int qw_audio_dss_next_g723_frame(const QwAudioDssSource *source,
                                 size_t position,
                                 QwAudioDssFrame *frame);

#endif
