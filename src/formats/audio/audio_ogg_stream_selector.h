#ifndef QUIREWISE_AUDIO_OGG_STREAM_SELECTOR_H
#define QUIREWISE_AUDIO_OGG_STREAM_SELECTOR_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_ogg_select_audio_stream(const uint8_t *data,
                                     size_t size,
                                     size_t requested_audio_track,
                                     uint8_t **selected_data,
                                     size_t *selected_size,
                                     size_t *audio_track_count);

#endif
