#ifndef QUIREWISE_AUDIO_OGG_FLAC_PARSER_H
#define QUIREWISE_AUDIO_OGG_FLAC_PARSER_H

#include "audio_flac_parser.h"
#include "audio_ogg_container.h"

typedef struct QwAudioOggFlacSource {
    QwAudioFlacSource flac;
    QwAudioOggPacketCursor packet_cursor;
} QwAudioOggFlacSource;

typedef struct QwAudioOggFlacFrame {
    const uint8_t *data;
    size_t size;
    uint32_t sample_count;
} QwAudioOggFlacFrame;

int qw_audio_ogg_flac_parse(const uint8_t *bytes,
                            size_t size,
                            QwAudioOggFlacSource *source,
                            QwConvertReport *report);
int qw_audio_ogg_flac_next_frame(QwAudioOggFlacSource *source,
                                 QwAudioOggPacketBuffer *packet_buffer,
                                 QwAudioOggFlacFrame *frame);

#endif
