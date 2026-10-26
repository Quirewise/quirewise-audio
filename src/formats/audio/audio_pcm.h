#ifndef QUIREWISE_AUDIO_PCM_H
#define QUIREWISE_AUDIO_PCM_H

#include "quirewise/convert.h"

#include <stdio.h>

int qw_audio_pcm_container_transcode(const void *data,
                                     size_t size,
                                     QwInputFormat input_format,
                                     QwOutputFormat output_format,
                                     size_t audio_track,
                                     uint8_t **out_data,
                                     size_t *out_size,
                                     QwConvertReport *report);
int qw_audio_pcm_container_transcode_stream(const void *data,
                                            size_t size,
                                            QwInputFormat input_format,
                                            QwOutputFormat output_format,
                                            size_t audio_track,
                                            FILE *stream,
                                            size_t max_output_bytes,
                                            size_t *out_size,
                                            QwConvertReport *report);

#endif
