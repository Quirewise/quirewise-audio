#include "audio_custom_wave_input.h"

#include "audio_pcm_parser.h"
#include "audio_wave_chunks.h"
#include "audio_wave_g711_parser.h"
#include "audio_wave_ima_parser.h"
#include "audio_wave_mpeg_parser.h"
#include "audio_wave_mpeg1_parser.h"
#include "audio_wave_gsm_parser.h"
#include "audio_wave_dts_parser.h"
#include "audio_wave_ms_parser.h"
#include "quirewise/status.h"

static int audio_custom_wave_parse_chunks(const QwAudioWaveChunks *chunks,
                                          QwAudioCustomInput *input)
{
    QwAudioPcmSource pcm = {0};
    int rc;

    rc = qw_audio_pcm_parse_wave_chunks(chunks, &pcm, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_PCM;
        input->sample_rate = pcm.sample_rate;
        input->channels = pcm.channels;
        input->channel_mask = pcm.channel_mask;
        input->sample_count = pcm.frames;
        input->has_metadata = pcm.has_metadata;
        input->pcm = pcm;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_ima_parse_chunks(chunks, &input->wave_ima, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_WAVE_IMA;
        input->sample_rate = input->wave_ima.sample_rate;
        input->channels = input->wave_ima.channels;
        input->sample_count = input->wave_ima.sample_count;
        input->has_metadata = input->wave_ima.has_metadata;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_ms_parse_chunks(chunks, &input->wave_ms, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = input->wave_ms.has_standard_coefficients
            ? QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM
            : QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS;
        input->sample_rate = input->wave_ms.sample_rate;
        input->channels = input->wave_ms.channels;
        input->sample_count = input->wave_ms.sample_count;
        input->has_metadata = input->wave_ms.has_metadata;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_g711_parse_chunks(chunks, &input->wave_g711, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = input->wave_g711.codec == QW_AUDIO_WAVE_G711_ALAW
            ? QW_AUDIO_CUSTOM_INPUT_WAVE_ALAW
            : QW_AUDIO_CUSTOM_INPUT_WAVE_MULAW;
        input->sample_rate = input->wave_g711.sample_rate;
        input->channels = input->wave_g711.channels;
        input->channel_mask = input->wave_g711.channel_mask;
        input->sample_count = input->wave_g711.sample_count;
        input->has_metadata = input->wave_g711.has_metadata;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_mpeg_parse_chunks(chunks, &input->wave_mpeg, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_WAVE_MP3;
        input->sample_rate = input->wave_mpeg.mpeg.sample_rate;
        input->channels = input->wave_mpeg.mpeg.channels;
        input->sample_count = input->wave_mpeg.sample_count;
        input->has_metadata = input->wave_mpeg.has_metadata;
        input->packet_position = input->wave_mpeg.mpeg.first_frame_offset;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_mpeg1_parse_chunks(chunks, &input->wave_mpeg1, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1;
        input->sample_rate = input->wave_mpeg1.mpeg.sample_rate;
        input->channels = input->wave_mpeg1.mpeg.channels;
        input->sample_count = input->wave_mpeg1.sample_count;
        input->has_metadata = input->wave_mpeg1.has_metadata;
        input->packet_position = input->wave_mpeg1.mpeg.first_frame_offset;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_gsm_parse_chunks(chunks, &input->wave_gsm_ms, NULL);
    if (rc == QW_STATUS_OK) {
        input->kind = QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS;
        input->sample_rate = input->wave_gsm_ms.sample_rate;
        input->channels = input->wave_gsm_ms.channels;
        input->sample_count = input->wave_gsm_ms.sample_count;
        input->has_metadata = input->wave_gsm_ms.has_metadata;
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;

    rc = qw_audio_wave_dts_parse_chunks(chunks, &input->wave_dts, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) return 0;
    if (rc != QW_STATUS_OK) return rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_WAVE_DTS;
    input->dts = input->wave_dts.dts;
    input->sample_rate = input->dts.sample_rate;
    input->channels = input->dts.channels;
    input->sample_count = input->wave_dts.sample_count;
    input->has_metadata = input->wave_dts.has_metadata;
    return 1;
}

int qw_audio_custom_wave_input_parse(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    QwAudioWaveChunks chunks = {0};
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_wave_parse_chunks(data, size, &chunks, NULL);
    if (rc != QW_STATUS_OK) return rc;
    return audio_custom_wave_parse_chunks(&chunks, input);
}

int qw_audio_custom_wave64_input_parse(const uint8_t *data,
                                       size_t size,
                                       QwAudioCustomInput *input)
{
    QwAudioWaveChunks chunks = {0};
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_wave64_parse_chunks(data, size, &chunks, NULL);
    if (rc != QW_STATUS_OK) return rc;
    return audio_custom_wave_parse_chunks(&chunks, input);
}
