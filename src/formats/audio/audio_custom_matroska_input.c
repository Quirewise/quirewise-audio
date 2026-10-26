#include "audio_custom_input.h"

#include "audio_custom_matroska_adpcm_validator.h"
#include "audio_custom_matroska_mpeg_validator.h"
#include "audio_custom_matroska_ms_adpcm_input.h"
#include "audio_custom_matroska_wavpack.h"
#include "quirewise/status.h"

#include <stdlib.h>

int qw_audio_custom_matroska_input_parse(const uint8_t *data,
                                         size_t size,
                                         QwInputFormat input_format,
                                         size_t requested_audio_track,
                                         QwAudioCustomInput *input)
{
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_matroska_parse(data, size, requested_audio_track,
                                 &input->matroska);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    if (input_format == QW_FORMAT_WEBM &&
        (!input->matroska.is_webm ||
         (input->matroska.codec != QW_AUDIO_MATROSKA_CODEC_OPUS &&
          input->matroska.codec != QW_AUDIO_MATROSKA_CODEC_VORBIS))) {
        return 0;
    }
    if (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_MS ||
        input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV) {
        rc = qw_audio_custom_matroska_adpcm_validate(
            &input->matroska);
        if (rc != QW_STATUS_OK) return rc;
    } else if (input->matroska.codec ==
               QW_AUDIO_MATROSKA_CODEC_WAVPACK) {
        rc = qw_audio_custom_matroska_wavpack_validate(
            &input->matroska);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
        if (rc != QW_STATUS_OK) return rc;
    }
    input->kind = QW_AUDIO_CUSTOM_INPUT_MATROSKA;
    input->sample_rate = input->matroska.sample_rate;
    input->channels = input->matroska.channels;
    input->channel_mask = input->matroska.channel_mask;
    input->sample_count = input->matroska.sample_count;
    input->has_metadata = 1;
    return 1;
}

int qw_audio_custom_matroska_input_next_packet(QwAudioCustomInput *input,
                                               QwAudioCustomPacket *packet)
{
    const uint8_t *data;
    const QwAudioMatroskaBlockAdditions *block_additions;
    size_t size;
    int64_t discard_padding_ns;
    int rc;
    if (!input || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_matroska_next_packet(
        &input->matroska, &data, &size, &discard_padding_ns,
        &block_additions);
    if (rc <= 0) return rc;
    packet->discard_padding_ns = discard_padding_ns;
    packet->block_additions = block_additions;
    if (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_MP1 ||
        input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_MP2 ||
        input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_MP3) {
        rc = qw_audio_custom_matroska_mpeg_validate_packet(
            &input->matroska, data, size);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_MS &&
        !input->matroska.wave_ms_format.has_standard_coefficients) {
        return qw_audio_custom_matroska_ms_adpcm_decode_packet(
            input, data, size, packet);
    }
    if (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_WAVPACK) {
        const uint8_t *reconstructed_data;
        size_t reconstructed_size;
        rc = qw_audio_custom_matroska_wavpack_reconstruct_packet(
            &input->matroska, data, size, &input->matroska_packet_data,
            &input->matroska_packet_capacity, &reconstructed_data,
            &reconstructed_size);
        if (rc != QW_STATUS_OK) return rc;
        packet->data = reconstructed_data;
        packet->size = reconstructed_size;
        packet->no_pts = 1u;
        return 1;
    }
    packet->data = data;
    packet->size = size;
    packet->no_pts = 1u;
    return 1;
}

void qw_audio_custom_matroska_input_dispose(QwAudioCustomInput *input)
{
    if (!input) return;
    free(input->matroska_packet_data);
    input->matroska_packet_data = NULL;
    input->matroska_packet_capacity = 0u;
}
