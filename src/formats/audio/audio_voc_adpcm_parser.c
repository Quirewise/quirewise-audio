#include "audio_voc_adpcm_parser.h"

#include "quirewise/status.h"

#include <limits.h>

int qw_audio_voc_adpcm_mode_from_packing_method(
    uint8_t packing_method,
    QwAudioVocAdpcmMode *mode)
{
    if (!mode) return 0;
    switch (packing_method) {
    case 1u:
        *mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4;
        return 1;
    case 2u:
        *mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3;
        return 1;
    case 3u:
        *mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2;
        return 1;
    default:
        *mode = QW_AUDIO_VOC_ADPCM_MODE_NONE;
        return 0;
    }
}

int qw_audio_voc_adpcm_mode_from_codec_tag(uint16_t codec_tag,
                                           uint16_t bits_per_sample,
                                           QwAudioVocAdpcmMode *mode)
{
    QwAudioVocAdpcmMode parsed_mode;
    if (!mode) return 0;
    switch (codec_tag) {
    case 1u:
        parsed_mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4;
        break;
    case 2u:
        parsed_mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3;
        break;
    case 3u:
        parsed_mode = QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2;
        break;
    case 0x0200u:
        parsed_mode = QW_AUDIO_VOC_ADPCM_MODE_CT_4;
        break;
    default:
        *mode = QW_AUDIO_VOC_ADPCM_MODE_NONE;
        return 0;
    }
    /* Type 1 stores coded width; Type 9 stores output depth. */
    if ((parsed_mode == QW_AUDIO_VOC_ADPCM_MODE_CT_4 &&
         bits_per_sample != 16u) ||
        (parsed_mode != QW_AUDIO_VOC_ADPCM_MODE_CT_4 &&
         bits_per_sample != 8u &&
         bits_per_sample !=
             qw_audio_voc_adpcm_mode_bits_per_sample(parsed_mode))) {
        *mode = QW_AUDIO_VOC_ADPCM_MODE_NONE;
        return 0;
    }
    *mode = parsed_mode;
    return 1;
}

uint16_t qw_audio_voc_adpcm_mode_bits_per_sample(QwAudioVocAdpcmMode mode)
{
    switch (mode) {
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4: return 4u;
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3: return 3u;
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2: return 2u;
    case QW_AUDIO_VOC_ADPCM_MODE_CT_4: return 4u;
    default: return 0u;
    }
}

int qw_audio_voc_adpcm_mode_supports_channels(QwAudioVocAdpcmMode mode,
                                             uint16_t channels)
{
    switch (mode) {
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4:
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3:
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2:
        return channels == 1u;
    case QW_AUDIO_VOC_ADPCM_MODE_CT_4:
        return channels == 1u || channels == 2u;
    default:
        return 0;
    }
}

int qw_audio_voc_adpcm_sample_count(QwAudioVocAdpcmMode mode,
                                    size_t data_size,
                                    uint16_t channels,
                                    int has_initial_sample,
                                    uint32_t *sample_count)
{
    size_t samples_per_byte;
    size_t count;
    if (!sample_count || !data_size) return QW_STATUS_ERR_FORMAT;
    if (!qw_audio_voc_adpcm_mode_supports_channels(mode, channels))
        return QW_STATUS_ERR_UNSUPPORTED;
    switch (mode) {
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4: samples_per_byte = 2u; break;
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3: samples_per_byte = 3u; break;
    case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2: samples_per_byte = 4u; break;
    case QW_AUDIO_VOC_ADPCM_MODE_CT_4:
        if (has_initial_sample) return QW_STATUS_ERR_FORMAT;
        samples_per_byte = channels == 1u ? 2u : 1u;
        break;
    default: return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (has_initial_sample) {
        if ((data_size - 1u) > (SIZE_MAX - 1u) / samples_per_byte)
            return QW_STATUS_ERR_FORMAT;
        count = 1u + (data_size - 1u) * samples_per_byte;
    } else {
        if (data_size > SIZE_MAX / samples_per_byte)
            return QW_STATUS_ERR_FORMAT;
        count = data_size * samples_per_byte;
    }
    if (count > UINT32_MAX) return QW_STATUS_ERR_FORMAT;
    *sample_count = (uint32_t)count;
    return QW_STATUS_OK;
}
