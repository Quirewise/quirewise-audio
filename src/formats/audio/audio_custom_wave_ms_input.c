#include "audio_custom_wave_ms_input.h"

#include "../../core/byte_utils.h"
#include "audio_wave_ms_decoder.h"
#include "quirewise/status.h"

#include <stdlib.h>

int qw_audio_custom_wave_ms_input_next_packet(QwAudioCustomInput *input,
                                              QwAudioCustomPacket *packet)
{
    QwAudioWaveMsFrame frame;
    size_t sample_count;
    size_t sample_values;
    size_t pcm_size;
    uint8_t *resized;
    int rc;

    if (!input || !packet ||
        input->kind != QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    rc = qw_audio_wave_ms_next_frame(&input->wave_ms, input->packet_index,
                                     &frame);
    if (rc <= 0) return rc < 0 ? rc : 0;
    if ((uint64_t)frame.sample_count >
        UINT64_MAX - input->packet_sample_position) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_count = (size_t)frame.sample_count;
    if (!qw_checked_size_mul(sample_count, input->wave_ms.channels,
                             &sample_values) ||
        !qw_checked_size_mul(sample_values, 2u, &pcm_size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (pcm_size > input->wave_ms_pcm_capacity) {
        resized = (uint8_t *)realloc(input->wave_ms_pcm_buffer, pcm_size);
        if (!resized) return QW_STATUS_ERR_OUT_OF_MEMORY;
        input->wave_ms_pcm_buffer = resized;
        input->wave_ms_pcm_capacity = pcm_size;
    }
    rc = qw_audio_wave_ms_decode_frame(&input->wave_ms, &frame,
                                       input->wave_ms_pcm_buffer, pcm_size);
    if (rc != QW_STATUS_OK) return rc;

    packet->data = input->wave_ms_pcm_buffer;
    packet->size = pcm_size;
    packet->pts = input->packet_sample_position;
    packet->duration = frame.sample_count;
    input->packet_index = frame.next_frame_index;
    input->packet_sample_position += frame.sample_count;
    return 1;
}
