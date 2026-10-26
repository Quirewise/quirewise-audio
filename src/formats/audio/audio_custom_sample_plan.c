#include "audio_custom_sample_plan.h"

void qw_audio_custom_sample_plan_build(const QwAudioCustomInput *input,
                                       QwAudioCustomSamplePlan *plan)
{
    if (!input || !plan) return;
    *plan = (QwAudioCustomSamplePlan){0};
    if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AAC ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_ALAC ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_MPEG ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_MACE ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_GSM ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_ILBC ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AMR ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AC3 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_QCELP ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_IMA4 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_G711 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_MP4 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_OPUS ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_FLAC ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_IMA ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM ||
        input->kind ==
            QW_AUDIO_CUSTOM_INPUT_WAVE_MS_ADPCM_CUSTOM_COEFFICIENTS ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MP3 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1 ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS ||
        input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_DTS ||
        (input->kind == QW_AUDIO_CUSTOM_INPUT_MPC &&
         input->mpc.is_sv8 &&
         input->sample_count > 0u) ||
        (input->kind == QW_AUDIO_CUSTOM_INPUT_ASF_WMA &&
         input->sample_count > 0u) ||
        (input->kind == QW_AUDIO_CUSTOM_INPUT_TAK &&
         input->sample_count > 0u) ||
        (input->kind == QW_AUDIO_CUSTOM_INPUT_MATROSKA &&
         (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_TTA ||
          (input->matroska.codec == QW_AUDIO_MATROSKA_CODEC_FLAC &&
           input->sample_count > 0u)))) {
        if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_IMA4) {
            plan->skip_samples = input->caf_ima4.priming_frames;
            plan->sample_limit = input->caf_ima4.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_G711) {
            plan->skip_samples = input->caf_g711.priming_frames;
            plan->sample_limit = input->caf_g711.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AAC) {
            plan->skip_samples = input->caf_aac.priming_frames;
            plan->sample_limit = input->caf_aac.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_ALAC) {
            plan->skip_samples = input->caf_alac.priming_frames;
            plan->sample_limit = input->caf_alac.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_MPEG) {
            plan->skip_samples = input->caf_mpeg.priming_frames;
            plan->sample_limit = input->caf_mpeg.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_MACE) {
            plan->skip_samples = input->caf_mace.priming_frames;
            plan->sample_limit = input->caf_mace.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_GSM) {
            plan->skip_samples = input->caf_gsm.priming_frames;
            plan->sample_limit = input->caf_gsm.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_ILBC) {
            plan->skip_samples = input->caf_ilbc.priming_frames;
            plan->sample_limit = input->caf_ilbc.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AMR) {
            plan->skip_samples = input->caf_amr.priming_frames;
            plan->sample_limit = input->caf_amr.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_AC3) {
            plan->skip_samples = input->caf_ac3.priming_frames;
            plan->sample_limit = input->caf_ac3.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_QDESIGN) {
            plan->skip_samples = input->caf_qdesign.priming_frames;
            plan->sample_limit = input->caf_qdesign.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_CAF_QCELP) {
            plan->skip_samples = input->caf_qcelp.priming_frames;
            plan->sample_limit = input->caf_qcelp.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_MP4) {
            plan->skip_samples = input->mp4.trim_start_samples;
            plan->leading_silence_samples =
                input->mp4.leading_silence_samples;
            plan->sample_limit = input->sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MP3) {
            plan->skip_samples =
                input->wave_mpeg.codec_delay_samples;
            plan->sample_limit = input->wave_mpeg.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_MPEG1) {
            plan->sample_limit = input->wave_mpeg1.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_GSM_MS) {
            plan->sample_limit = input->wave_gsm_ms.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_DTS) {
            plan->sample_limit = input->wave_dts.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_OPUS) {
            plan->sample_limit = input->ogg_opus.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_FLAC) {
            plan->sample_limit = input->ogg_flac.flac.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS) {
            plan->sample_limit = input->ogg_vorbis.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX) {
            plan->sample_limit = input->ogg_speex.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_WAVE_IMA) {
            plan->sample_limit = input->wave_ima.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_MPC) {
            plan->sample_limit = input->sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_ASF_WMA) {
            plan->sample_limit = input->sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_TAK) {
            plan->sample_limit = input->tak.sample_count;
        } else if (input->kind == QW_AUDIO_CUSTOM_INPUT_MATROSKA) {
            plan->sample_limit = input->sample_count;
        } else {
            plan->sample_limit = input->wave_ms.sample_count;
        }
        plan->limit_enabled = 1;
    }
    if (input->kind == QW_AUDIO_CUSTOM_INPUT_MATROSKA &&
        input->matroska.codec_delay_ns) {
        uint64_t whole_seconds =
            input->matroska.codec_delay_ns / UINT64_C(1000000000);
        uint64_t remainder =
            input->matroska.codec_delay_ns % UINT64_C(1000000000);
        plan->skip_samples =
            whole_seconds * input->sample_rate +
            remainder * input->sample_rate / UINT64_C(1000000000);
    }
}