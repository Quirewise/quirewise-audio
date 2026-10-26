#include "audio_custom_pcm_input.h"

#include "audio_caf_parser.h"
#include "audio_wave64_parser.h"
#include "quirewise/status.h"

static int audio_custom_parse_pcm(const uint8_t *data,
                                  size_t size,
                                  QwInputFormat input_format,
                                  QwAudioPcmSource *pcm)
{
    switch (input_format) {
    case QW_FORMAT_WAV:
        return qw_audio_pcm_parse_wave(data, size, pcm, NULL);
    case QW_FORMAT_AIFF:
        return qw_audio_pcm_parse_aiff(data, size, pcm, NULL);
    case QW_FORMAT_AU:
        return qw_audio_au_parse_pcm(data, size, pcm, NULL);
    case QW_FORMAT_W64:
        return qw_audio_wave64_parse_pcm(data, size, pcm, NULL);
    case QW_FORMAT_CAF:
        return qw_audio_caf_parse_pcm(data, size, pcm, NULL);
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}

static void audio_custom_set_pcm(QwAudioCustomInput *input,
                                 const QwAudioPcmSource *pcm)
{
    input->kind = QW_AUDIO_CUSTOM_INPUT_PCM;
    input->sample_rate = pcm->sample_rate;
    input->channels = pcm->channels;
    input->channel_mask = pcm->channel_mask;
    input->sample_count = pcm->frames;
    input->has_metadata = pcm->has_metadata;
    input->pcm = *pcm;
}

int qw_audio_custom_pcm_input_parse(const uint8_t *data,
                                    size_t size,
                                    QwInputFormat input_format,
                                    QwAudioCustomInput *input)
{
    QwAudioPcmSource pcm = {0};
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = audio_custom_parse_pcm(data, size, input_format, &pcm);
    if (rc == QW_STATUS_OK) {
        audio_custom_set_pcm(input, &pcm);
        return 1;
    }
    if (rc != QW_STATUS_ERR_UNSUPPORTED) return rc;
    return 0;
}
