#include "audio_mp4_aac_config_parser.h"

#include "audio_aac_specific_config_parser.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_mp4_parse_aac_config(const uint8_t *config,
                                  size_t config_size,
                                  uint32_t sample_rate,
                                  uint16_t entry_channels,
                                  QwAudioMp4Source *source)
{
    QwAudioAacSpecificConfig parsed;
    int rc;
    if (!config || !source || !sample_rate || !entry_channels ||
        config_size > QW_AUDIO_MP4_CONFIG_MAX_SIZE) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    rc = qw_audio_aac_specific_config_parse(config, config_size, &parsed);
    if (rc != QW_STATUS_OK) return rc;
    if (parsed.output_channels != entry_channels ||
        parsed.output_sample_rate != sample_rate) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    memcpy(source->config, config, config_size);
    source->config_size = config_size;
    source->audio_object_type = parsed.object_type;
    source->channel_configuration = parsed.channel_configuration;
    source->channels = entry_channels;
    source->ps_present = parsed.ps_present;
    source->sample_rate = sample_rate;
    source->codec = QW_AUDIO_MP4_CODEC_AAC;
    return QW_STATUS_OK;
}
