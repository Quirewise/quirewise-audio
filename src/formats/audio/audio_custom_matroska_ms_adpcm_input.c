#include "audio_custom_matroska_ms_adpcm_input.h"

#include "../../core/byte_utils.h"
#include "audio_wave_ms_decoder.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdlib.h>

int qw_audio_custom_matroska_ms_adpcm_decode_packet(
    QwAudioCustomInput *input,
    const uint8_t *data,
    size_t size,
    QwAudioCustomPacket *packet)
{
    const QwAudioWaveMsFormat *format;
    QwAudioWaveMsSource source = {0};
    size_t block_count;
    size_t sample_count;
    size_t sample_values;
    size_t pcm_size;
    size_t block_index;
    uint8_t *resized;

    if (!input || !data || !size || !packet ||
        input->kind != QW_AUDIO_CUSTOM_INPUT_MATROSKA ||
        input->matroska.codec != QW_AUDIO_MATROSKA_CODEC_ADPCM_MS) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    format = &input->matroska.wave_ms_format;
    if (!format->block_align || !format->samples_per_block ||
        !format->channels || !format->coefficient_count ||
        !format->coefficients || size % format->block_align != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    block_count = size / format->block_align;
    if (!block_count ||
        !qw_checked_size_mul(block_count, format->samples_per_block,
                             &sample_count) ||
        sample_count > UINT32_MAX ||
        !qw_checked_size_mul(sample_count, format->channels,
                             &sample_values) ||
        !qw_checked_size_mul(sample_values, 2u, &pcm_size) ||
        pcm_size > (size_t)INT_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }

    source.data = data;
    source.size = size;
    source.frame_count = block_count;
    source.sample_count = sample_count;
    source.sample_rate = format->sample_rate;
    source.block_align = format->block_align;
    source.samples_per_block = format->samples_per_block;
    source.coefficient_count = format->coefficient_count;
    source.coefficients = format->coefficients;
    source.channels = format->channels;
    source.has_standard_coefficients = format->has_standard_coefficients;

    if (pcm_size > input->matroska_packet_capacity) {
        resized = (uint8_t *)realloc(input->matroska_packet_data, pcm_size);
        if (!resized) return QW_STATUS_ERR_OUT_OF_MEMORY;
        input->matroska_packet_data = resized;
        input->matroska_packet_capacity = pcm_size;
    }
    for (block_index = 0u; block_index < block_count; ++block_index) {
        QwAudioWaveMsFrame frame;
        size_t decoded_offset;
        int rc = qw_audio_wave_ms_next_frame(&source, block_index, &frame);
        if (rc != 1 ||
            !qw_checked_size_mul(block_index,
                                 (size_t)format->samples_per_block,
                                 &decoded_offset) ||
            !qw_checked_size_mul(decoded_offset, format->channels,
                                 &decoded_offset) ||
            !qw_checked_size_mul(decoded_offset, 2u, &decoded_offset) ||
            decoded_offset > pcm_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = qw_audio_wave_ms_decode_frame(
            &source, &frame, input->matroska_packet_data + decoded_offset,
            pcm_size - decoded_offset);
        if (rc != QW_STATUS_OK) return rc;
    }

    packet->data = input->matroska_packet_data;
    packet->size = pcm_size;
    packet->duration = (uint32_t)sample_count;
    packet->no_pts = 1u;
    return 1;
}
