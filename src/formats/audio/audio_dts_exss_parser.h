#ifndef QUIREWISE_AUDIO_DTS_EXSS_PARSER_H
#define QUIREWISE_AUDIO_DTS_EXSS_PARSER_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_dts_exss_frame_size(const uint8_t *data,
                                 size_t size,
                                 size_t *frame_size);

#endif
