#ifndef QUIREWISE_AUDIO_CUSTOM_MATROSKA_MPEG_VALIDATOR_H
#define QUIREWISE_AUDIO_CUSTOM_MATROSKA_MPEG_VALIDATOR_H

#include "audio_matroska_parser.h"

int qw_audio_custom_matroska_mpeg_validate_packet(
        const QwAudioMatroskaSource *source,
        const uint8_t *bytes,
        size_t size);

#endif
