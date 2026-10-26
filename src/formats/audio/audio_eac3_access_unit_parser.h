#ifndef QUIREWISE_AUDIO_EAC3_ACCESS_UNIT_PARSER_H
#define QUIREWISE_AUDIO_EAC3_ACCESS_UNIT_PARSER_H

#include "audio_dolby_digital_internal.h"

int qw_audio_eac3_select_substream(
    const uint8_t *bytes,
    size_t size,
    QwAudioDolbyDigitalFrameHeader *selected_header);

int qw_audio_eac3_next_access_unit(
    const QwAudioDolbyDigitalSource *source,
    size_t position,
    QwAudioDolbyDigitalFrame *frame);

#endif
