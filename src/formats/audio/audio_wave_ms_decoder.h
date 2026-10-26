#ifndef QUIREWISE_AUDIO_WAVE_MS_DECODER_H
#define QUIREWISE_AUDIO_WAVE_MS_DECODER_H

#include "audio_wave_ms_parser.h"

#include <stddef.h>
#include <stdint.h>

/* Decodes one parsed Microsoft ADPCM block to interleaved PCM S16LE. */
int qw_audio_wave_ms_decode_frame(const QwAudioWaveMsSource *source,
                                  const QwAudioWaveMsFrame *frame,
                                  uint8_t *pcm_s16le,
                                  size_t pcm_capacity);

#endif
