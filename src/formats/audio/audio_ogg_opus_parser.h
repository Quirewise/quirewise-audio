#ifndef QUIREWISE_AUDIO_OGG_OPUS_PARSER_H
#define QUIREWISE_AUDIO_OGG_OPUS_PARSER_H

#include "audio_ogg_container.h"
#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_OGG_OPUS_MAX_HEAD_SIZE = 276u,
    QW_AUDIO_OGG_OPUS_MAX_CHANNELS = 64u
};

typedef struct QwAudioOggOpusSource {
    const uint8_t *data;
    size_t size;
    size_t first_audio_page_offset;
    size_t first_audio_segment_index;
    size_t frame_data_end;
    uint64_t sample_count;
    QwAudioOggPacketCursor packet_cursor;
    uint32_t serial_number;
    uint32_t sample_rate;
    uint16_t pre_skip;
    uint16_t decoder_channels;
    uint8_t channels;
    uint8_t mapping_family;
    uint8_t opus_head[QW_AUDIO_OGG_OPUS_MAX_HEAD_SIZE];
    size_t opus_head_size;
    double *demixing_matrix;
    int demixing_matrix_stride;
    int has_metadata;
} QwAudioOggOpusSource;

typedef struct QwAudioOggOpusFrame {
    const uint8_t *data;
    size_t size;
    uint32_t sample_count;
} QwAudioOggOpusFrame;

int qw_audio_ogg_opus_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioOggOpusSource *source,
                            QwConvertReport *report);
void qw_audio_ogg_opus_source_dispose(QwAudioOggOpusSource *source);
int qw_audio_ogg_opus_next_frame(QwAudioOggOpusSource *source,
                                 QwAudioOggPacketBuffer *packet_buffer,
                                 QwAudioOggOpusFrame *frame);

#endif
