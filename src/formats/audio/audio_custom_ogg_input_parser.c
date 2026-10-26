#include "audio_custom_ogg_input_parser.h"

#include "quirewise/status.h"

typedef int (*QwAudioOggInputCodecParser)(const uint8_t *data,
                                          size_t size,
                                          QwAudioCustomInput *input);

static int audio_custom_ogg_parse_opus(const uint8_t *data,
                                       size_t size,
                                       QwAudioCustomInput *input)
{
    int rc = qw_audio_ogg_opus_parse(data, size, &input->ogg_opus, NULL);
    if (rc != QW_STATUS_OK) return rc == QW_STATUS_ERR_UNSUPPORTED ? 0 : rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_OGG_OPUS;
    input->sample_rate = input->ogg_opus.sample_rate;
    input->channels = input->ogg_opus.channels;
    input->sample_count = input->ogg_opus.sample_count;
    input->has_metadata = input->ogg_opus.has_metadata;
    return 1;
}

static int audio_custom_ogg_parse_flac(const uint8_t *data,
                                       size_t size,
                                       QwAudioCustomInput *input)
{
    int rc = qw_audio_ogg_flac_parse(data, size, &input->ogg_flac, NULL);
    if (rc != QW_STATUS_OK) return rc == QW_STATUS_ERR_UNSUPPORTED ? 0 : rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_OGG_FLAC;
    input->sample_rate = input->ogg_flac.flac.sample_rate;
    input->channels = input->ogg_flac.flac.channels;
    input->sample_count = input->ogg_flac.flac.sample_count;
    input->has_metadata = 1;
    return 1;
}

static int audio_custom_ogg_parse_speex(const uint8_t *data,
                                        size_t size,
                                        QwAudioCustomInput *input)
{
    int rc = qw_audio_ogg_speex_parse(data, size, &input->ogg_speex, NULL);
    if (rc != QW_STATUS_OK) return rc == QW_STATUS_ERR_UNSUPPORTED ? 0 : rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_OGG_SPEEX;
    input->sample_rate = input->ogg_speex.sample_rate;
    input->channels = input->ogg_speex.channels;
    input->sample_count = input->ogg_speex.sample_count;
    input->has_metadata = 1;
    return 1;
}

static int audio_custom_ogg_parse_vorbis(const uint8_t *data,
                                         size_t size,
                                         QwAudioCustomInput *input)
{
    int rc = qw_audio_ogg_vorbis_parse(data, size, &input->ogg_vorbis, NULL);
    if (rc != QW_STATUS_OK) return rc == QW_STATUS_ERR_UNSUPPORTED ? 0 : rc;
    input->kind = QW_AUDIO_CUSTOM_INPUT_OGG_VORBIS;
    input->sample_rate = input->ogg_vorbis.sample_rate;
    input->channels = input->ogg_vorbis.channels;
    input->sample_count = input->ogg_vorbis.sample_count;
    input->has_metadata = 1;
    return 1;
}

int qw_audio_custom_ogg_input_parse_selected(const uint8_t *data,
                                             size_t size,
                                             QwAudioCustomInput *input)
{
    static const QwAudioOggInputCodecParser parsers[] = {
        audio_custom_ogg_parse_opus,
        audio_custom_ogg_parse_flac,
        audio_custom_ogg_parse_speex,
        audio_custom_ogg_parse_vorbis
    };
    size_t i;
    int rc;
    if (!data || !size || !input) return QW_STATUS_ERR_INVALID_ARGUMENT;
    for (i = 0u; i < sizeof(parsers) / sizeof(parsers[0]); ++i) {
        rc = parsers[i](data, size, input);
        if (rc != 0) return rc;
    }
    return 0;
}
