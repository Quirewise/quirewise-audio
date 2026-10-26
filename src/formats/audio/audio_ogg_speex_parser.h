#ifndef QUIREWISE_AUDIO_OGG_SPEEX_PARSER_H
#define QUIREWISE_AUDIO_OGG_SPEEX_PARSER_H

#include "audio_ogg_container.h"
#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_OGG_SPEEX_HEADER_SIZE = 80u };

typedef struct QwAudioOggSpeexSource {
    uint32_t sample_rate;
    uint32_t samples_per_packet;
    uint64_t sample_count;
    uint64_t audio_bytes;
    uint8_t channels;
    uint8_t speex_header[QW_AUDIO_OGG_SPEEX_HEADER_SIZE];
    QwAudioOggPacketCursor packet_cursor;
} QwAudioOggSpeexSource;

typedef struct QwAudioOggSpeexFrame {
    const uint8_t *data;
    size_t size;
    uint32_t sample_count;
} QwAudioOggSpeexFrame;

int qw_audio_ogg_speex_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioOggSpeexSource *source,
                             QwConvertReport *report);
int qw_audio_ogg_speex_next_frame(QwAudioOggSpeexSource *source,
                                  QwAudioOggPacketBuffer *packet_buffer,
                                  QwAudioOggSpeexFrame *frame);

#endif
