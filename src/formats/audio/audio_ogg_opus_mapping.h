#ifndef QUIREWISE_AUDIO_OGG_OPUS_MAPPING_H
#define QUIREWISE_AUDIO_OGG_OPUS_MAPPING_H

#include "audio_ogg_opus_parser.h"

#include <stddef.h>

int qw_audio_ogg_opus_build_resampler_matrix(
    const QwAudioOggOpusSource *source,
    int output_channels,
    double *matrix_buffer,
    size_t matrix_buffer_capacity,
    const double **matrix,
    int *stride);

#endif
