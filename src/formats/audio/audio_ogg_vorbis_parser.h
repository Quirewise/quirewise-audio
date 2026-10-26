#ifndef QUIREWISE_AUDIO_OGG_VORBIS_PARSER_H
#define QUIREWISE_AUDIO_OGG_VORBIS_PARSER_H

#include "audio_ogg_container.h"
#include "quirewise/convert.h"

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioOggVorbisSource {
    uint32_t sample_rate;
    uint64_t sample_count;
    uint64_t audio_bytes;
    uint8_t channels;
    uint8_t *decoder_extradata;
    size_t decoder_extradata_size;
    QwAudioOggPacketCursor packet_cursor;
} QwAudioOggVorbisSource;

typedef struct QwAudioOggVorbisFrame {
    const uint8_t *data;
    size_t size;
} QwAudioOggVorbisFrame;

int qw_audio_ogg_vorbis_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioOggVorbisSource *source,
                              QwConvertReport *report);
int qw_audio_ogg_vorbis_next_frame(QwAudioOggVorbisSource *source,
                                   QwAudioOggPacketBuffer *packet_buffer,
                                   QwAudioOggVorbisFrame *frame);
void qw_audio_ogg_vorbis_source_dispose(QwAudioOggVorbisSource *source);

#endif
