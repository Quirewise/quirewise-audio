#include "audio_custom_aiff_input.h"

#include "audio_aiff_g711_parser.h"
#include "audio_custom_aiff_codec_input.h"
#include "audio_custom_aiff_ima4_input.h"
#include "audio_custom_aiff_mace_input.h"
#include "quirewise/status.h"

int qw_audio_custom_aiff_input_parse(const uint8_t *data,
                                     size_t size,
                                     QwAudioCustomInput *input)
{
    int rc;

    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_custom_aiff_codec_input_parse(data, size, input);
    if (rc != 0) return rc;
    rc = qw_audio_aiff_g711_parse(data, size, &input->aiff_g711, NULL);
    if (rc == QW_STATUS_ERR_UNSUPPORTED) {
        rc = qw_audio_custom_aiff_ima4_input_parse(data, size, input);
        if (rc != 0) return rc;
        return qw_audio_custom_aiff_mace_input_parse(data, size, input);
    }
    if (rc != QW_STATUS_OK) return rc;
    input->kind = input->aiff_g711.codec == QW_AUDIO_AIFF_G711_ALAW
        ? QW_AUDIO_CUSTOM_INPUT_AIFF_ALAW
        : QW_AUDIO_CUSTOM_INPUT_AIFF_MULAW;
    input->sample_rate = input->aiff_g711.sample_rate;
    input->channels = input->aiff_g711.channels;
    input->sample_count = input->aiff_g711.sample_count;
    input->has_metadata = input->aiff_g711.has_metadata;
    return 1;
}
