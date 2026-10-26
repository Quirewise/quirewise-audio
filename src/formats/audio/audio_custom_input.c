#include "audio_custom_input.h"
#include "audio_custom_au_input.h"
#include "audio_custom_aiff_input.h"
#include "audio_custom_pcm_input.h"
#include "audio_custom_wave_input.h"
#include "audio_custom_mpc_input.h"
#include "audio_custom_asf_input.h"
#include "audio_custom_dff_input.h"
#include "audio_custom_dss_input.h"
#include "audio_custom_dsf_input.h"
#include "audio_custom_mp4_input.h"

#include "audio_custom_caf_input.h"
#include "audio_custom_ogg_input.h"
#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

static int audio_custom_input_parser_result(int rc)
{
    return rc == 0 ? QW_STATUS_ERR_UNSUPPORTED : rc;
}

int qw_audio_custom_input_parse(const uint8_t *data,
                                size_t size,
                                QwInputFormat input_format,
                                size_t requested_audio_track,
                                QwAudioCustomInput *input)
{
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(input, 0, sizeof(*input));
    if (input_format == QW_FORMAT_WAV) {
        return audio_custom_input_parser_result(
            qw_audio_custom_wave_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_W64) {
        return audio_custom_input_parser_result(
            qw_audio_custom_wave64_input_parse(data, size, input));
    }
    rc = qw_audio_custom_pcm_input_parse(data, size, input_format, input);
    if (rc != 0) return rc;

    if (input_format == QW_FORMAT_AIFF) {
        return audio_custom_input_parser_result(
            qw_audio_custom_aiff_input_parse(data, size, input));
    }

    if (input_format == QW_FORMAT_CAF) {
        return audio_custom_input_parser_result(
            qw_audio_custom_caf_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_DSF) {
        return audio_custom_input_parser_result(
            qw_audio_custom_dsf_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_DFF) {
        return audio_custom_input_parser_result(
            qw_audio_custom_dff_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_DSS) {
        return audio_custom_input_parser_result(
            qw_audio_custom_dss_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_M4A) {
        return audio_custom_input_parser_result(
            qw_audio_custom_mp4_input_parse(data, size,
                                            requested_audio_track, input));
    }
    if (input_format == QW_FORMAT_MATROSKA ||
        input_format == QW_FORMAT_WEBM) {
        return audio_custom_input_parser_result(
            qw_audio_custom_matroska_input_parse(
                data, size, input_format, requested_audio_track, input));
    }
    if (input_format == QW_FORMAT_MPC) {
        return audio_custom_input_parser_result(
            qw_audio_custom_mpc_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_WMA) {
        return audio_custom_input_parser_result(
            qw_audio_custom_asf_input_parse(data, size,
                                            requested_audio_track, input));
    }
    if (input_format == QW_FORMAT_AU) {
        return audio_custom_input_parser_result(
            qw_audio_custom_au_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_AAC) {
        rc = qw_audio_adts_parse(data, size, &input->adts, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_AAC_ADTS;
        input->sample_rate = input->adts.sample_rate;
        input->channels = input->adts.channels;
        input->sample_count = input->adts.sample_count;
        input->has_metadata = input->adts.has_metadata;
        input->packet_position = input->adts.first_frame_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_FLAC) {
        rc = qw_audio_flac_parse(data, size, &input->flac, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_FLAC;
        input->sample_rate = input->flac.sample_rate;
        input->channels = input->flac.channels;
        input->sample_count = input->flac.sample_count;
        input->packet_position = input->flac.first_frame_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_MP2 || input_format == QW_FORMAT_MP3) {
        rc = qw_audio_mpeg_parse(data, size, input_format, &input->mpeg, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = input_format == QW_FORMAT_MP2
            ? QW_AUDIO_CUSTOM_INPUT_MP2 : QW_AUDIO_CUSTOM_INPUT_MP3;
        input->sample_rate = input->mpeg.sample_rate;
        input->channels = input->mpeg.channels;
        input->sample_count = input->mpeg.sample_count;
        input->has_metadata = input->mpeg.has_metadata;
        input->packet_position = input->mpeg.first_frame_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_AMR) {
        rc = qw_audio_amr_parse(data, size, &input->amr, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = input->amr.wideband
            ? QW_AUDIO_CUSTOM_INPUT_AMR_WB
            : QW_AUDIO_CUSTOM_INPUT_AMR_NB;
        input->sample_rate = input->amr.sample_rate;
        input->channels = input->amr.channels;
        input->sample_count = input->amr.sample_count;
        input->packet_position = input->amr.first_frame_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_AC3) {
        rc = qw_audio_dolby_digital_parse(
            data, size, &input->dolby_digital, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind =
            input->dolby_digital.codec == QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3
            ? QW_AUDIO_CUSTOM_INPUT_EAC3 : QW_AUDIO_CUSTOM_INPUT_AC3;
        input->sample_rate = input->dolby_digital.sample_rate;
        input->channels = input->dolby_digital.channels;
        input->sample_count = input->dolby_digital.sample_count;
        return 1;
    }
    if (input_format == QW_FORMAT_DTS) {
        rc = qw_audio_dts_parse(data, size, &input->dts, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_DTS;
        input->sample_rate = input->dts.sample_rate;
        input->channels = input->dts.channels;
        input->sample_count = input->dts.sample_count;
        return 1;
    }
    if (input_format == QW_FORMAT_TTA) {
        rc = qw_audio_tta_parse(data, size, &input->tta, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_TTA;
        input->sample_rate = input->tta.sample_rate;
        input->channels = input->tta.channels;
        input->sample_count = input->tta.sample_count;
        input->has_metadata = input->tta.has_metadata;
        input->packet_position = input->tta.frame_data_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_TAK) {
        return audio_custom_input_parser_result(
            qw_audio_custom_tak_input_parse(data, size, input));
    }
    if (input_format == QW_FORMAT_WAVPACK) {
        rc = qw_audio_wavpack_parse(data, size, &input->wavpack, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_WAVPACK;
        input->sample_rate = input->wavpack.sample_rate;
        input->channels = input->wavpack.channels;
        input->sample_count = input->wavpack.sample_count;
        input->has_metadata =
            input->wavpack.first_frame_offset > 0u ||
            input->wavpack.frame_data_end < size;
        input->packet_position = input->wavpack.first_frame_offset;
        return 1;
    }
    if (input_format == QW_FORMAT_VOC) {
        rc = qw_audio_voc_parse(data, size, &input->voc, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        switch (input->voc.codec) {
        case QW_AUDIO_VOC_CODEC_PCM_U8:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_U8;
            break;
        case QW_AUDIO_VOC_CODEC_PCM_S16LE:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_S16LE;
            break;
        case QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_4;
            break;
        case QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_3:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_3;
            break;
        case QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_SBPRO_2;
            break;
        case QW_AUDIO_VOC_CODEC_ADPCM_CT:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_ADPCM_CT;
            break;
        case QW_AUDIO_VOC_CODEC_PCM_ALAW:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_ALAW;
            break;
        case QW_AUDIO_VOC_CODEC_PCM_MULAW:
            input->kind = QW_AUDIO_CUSTOM_INPUT_VOC_MULAW;
            break;
        default:
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        input->sample_rate = input->voc.sample_rate;
        input->channels = input->voc.channels;
        input->sample_count = input->voc.sample_count;
        input->has_metadata = input->voc.has_metadata;
        return 1;
    }
    if (input_format == QW_FORMAT_APE) {
        rc = qw_audio_ape_parse(data, size, &input->ape, NULL);
        if (rc == QW_STATUS_ERR_UNSUPPORTED) return QW_STATUS_ERR_UNSUPPORTED;
        if (rc != QW_STATUS_OK) return rc;
        input->kind = QW_AUDIO_CUSTOM_INPUT_APE;
        input->sample_rate = input->ape.sample_rate;
        input->channels = input->ape.channels;
        input->sample_count = input->ape.sample_count;
        input->has_metadata = input->ape.has_metadata;
        return 1;
    }
    if (input_format == QW_FORMAT_OGG) {
        return audio_custom_input_parser_result(
            qw_audio_custom_ogg_input_parse(
                data, size, requested_audio_track, input));
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

void qw_audio_custom_input_dispose(QwAudioCustomInput *input)
{
    if (!input) return;
    free(input->wave_ms_pcm_buffer);
    input->wave_ms_pcm_buffer = NULL;
    input->wave_ms_pcm_capacity = 0u;
    qw_audio_custom_dsf_input_dispose(input);
    qw_audio_custom_ogg_input_dispose(input);
    qw_audio_custom_asf_input_dispose(input);
    qw_audio_custom_matroska_input_dispose(input);
    qw_audio_mp4_source_dispose(&input->mp4);
}
