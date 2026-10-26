#include "audio_ogg_opus_mapping.h"

#include "quirewise/status.h"

#include <string.h>

static uint16_t audio_ogg_opus_ambisonic_channels(uint8_t channels,
                                                  int *has_stereo)
{
    uint16_t order;
    if (has_stereo) *has_stereo = 0;
    for (order = 1u; order <= 8u; ++order) {
        uint16_t ambisonic_count = (uint16_t)(order * order);
        if (channels == ambisonic_count) return ambisonic_count;
        if (channels == ambisonic_count + 2u) {
            if (has_stereo) *has_stereo = 1;
            return ambisonic_count;
        }
    }
    return 0u;
}

int qw_audio_ogg_opus_build_resampler_matrix(
    const QwAudioOggOpusSource *source,
    int output_channels,
    double *matrix_buffer,
    size_t matrix_buffer_capacity,
    const double **matrix,
    int *stride)
{
    uint16_t ambisonic_channels;
    size_t required_coefficients;
    size_t input_channel;
    size_t input_channels;
    int has_nondiegetic_stereo = 0;

    if (!source || !matrix || !stride || !source->demixing_matrix ||
        !source->channels || !source->decoder_channels ||
        source->channels > QW_AUDIO_OGG_OPUS_MAX_CHANNELS ||
        source->decoder_channels > QW_AUDIO_OGG_OPUS_MAX_CHANNELS ||
        source->demixing_matrix_stride != (int)source->decoder_channels) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    input_channels = source->decoder_channels;
    *matrix = source->demixing_matrix;
    *stride = (int)input_channels;
    if (output_channels == (int)source->channels) return QW_STATUS_OK;
    if (output_channels != 1 && output_channels != 2)
        return QW_STATUS_ERR_UNSUPPORTED;

    required_coefficients = (size_t)output_channels * input_channels;
    if (!matrix_buffer || matrix_buffer_capacity < required_coefficients)
        return QW_STATUS_ERR_INVALID_ARGUMENT;

    if (source->channels == 1u && output_channels == 2) {
        memcpy(matrix_buffer, source->demixing_matrix,
               input_channels * sizeof(*matrix_buffer));
        memcpy(matrix_buffer + input_channels, source->demixing_matrix,
               input_channels * sizeof(*matrix_buffer));
    } else if (output_channels == 1) {
        /* The first Ambisonic channel W is valid for mono playback. */
        memcpy(matrix_buffer, source->demixing_matrix,
               input_channels * sizeof(*matrix_buffer));
    } else {
        ambisonic_channels = audio_ogg_opus_ambisonic_channels(
            source->channels, &has_nondiegetic_stereo);
        if (!ambisonic_channels) return QW_STATUS_ERR_UNSUPPORTED;
        for (input_channel = 0u; input_channel < input_channels;
             ++input_channel) {
            double w = source->demixing_matrix[input_channel];
            double y = ambisonic_channels > 1u
                ? source->demixing_matrix[input_channels + input_channel]
                : 0.0;
            if (has_nondiegetic_stereo) {
                size_t left_row = ambisonic_channels;
                size_t right_row = (size_t)ambisonic_channels + 1u;
                matrix_buffer[input_channel] = 0.25 * w + 0.25 * y +
                    0.5 * source->demixing_matrix[
                        left_row * input_channels + input_channel];
                matrix_buffer[input_channels + input_channel] =
                    0.25 * w - 0.25 * y +
                    0.5 * source->demixing_matrix[
                        right_row * input_channels + input_channel];
            } else if (ambisonic_channels > 1u) {
                matrix_buffer[input_channel] = 0.5 * w + 0.5 * y;
                matrix_buffer[input_channels + input_channel] =
                    0.5 * w - 0.5 * y;
            } else {
                matrix_buffer[input_channel] = w;
                matrix_buffer[input_channels + input_channel] = w;
            }
        }
    }
    *matrix = matrix_buffer;
    *stride = (int)input_channels;
    return QW_STATUS_OK;
}
