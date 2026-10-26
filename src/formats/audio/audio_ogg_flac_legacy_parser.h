#ifndef QUIREWISE_AUDIO_OGG_FLAC_LEGACY_PARSER_H
#define QUIREWISE_AUDIO_OGG_FLAC_LEGACY_PARSER_H

#include "audio_flac_parser.h"
#include "audio_ogg_container.h"

int qw_audio_ogg_flac_legacy_parse_headers(
        const uint8_t *bytes,
        size_t size,
        QwAudioFlacSource *flac,
        QwAudioOggPacketBuffer *packet_buffer,
        QwAudioOggPacketCursor *first_audio_cursor);

#endif
