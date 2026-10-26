#include "audio_au_adpcm_parser.h"

#include "audio_au_parser.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_au_adpcm_describe(uint32_t encoding,
                               QwAudioAuAdpcmInfo *info)
{
    QwAudioAuAdpcmInfo parsed = {0};
    switch (encoding) {
    case QW_AUDIO_AU_ENCODING_G721_32:
        parsed.codec = QW_AUDIO_AU_ADPCM_CODEC_G726_LE;
        parsed.bits_per_sample = 4u;
        parsed.packet_alignment = 1u;
        parsed.decoder_block_align = 4u;
        break;
    case QW_AUDIO_AU_ENCODING_G722:
        parsed.codec = QW_AUDIO_AU_ADPCM_CODEC_G722;
        parsed.bits_per_sample = 4u;
        parsed.packet_alignment = 1u;
        parsed.decoder_block_align = 1u;
        break;
    case QW_AUDIO_AU_ENCODING_G723_24:
        parsed.codec = QW_AUDIO_AU_ADPCM_CODEC_G726_LE;
        parsed.bits_per_sample = 3u;
        parsed.packet_alignment = 3u;
        parsed.decoder_block_align = 3u;
        break;
    case QW_AUDIO_AU_ENCODING_G723_40:
        parsed.codec = QW_AUDIO_AU_ADPCM_CODEC_G726_LE;
        parsed.bits_per_sample = 5u;
        parsed.packet_alignment = 5u;
        parsed.decoder_block_align = 5u;
        break;
    case QW_AUDIO_AU_ENCODING_G726_16:
        parsed.codec = QW_AUDIO_AU_ADPCM_CODEC_G726_LE;
        parsed.bits_per_sample = 2u;
        parsed.packet_alignment = 1u;
        parsed.decoder_block_align = 1u;
        break;
    default:
        if (info) memset(info, 0, sizeof(*info));
        return 0;
    }
    if (info) *info = parsed;
    return 1;
}

int qw_audio_au_adpcm_sample_count(uint32_t encoding,
                                   size_t data_size,
                                   uint16_t channels,
                                   uint64_t *sample_count)
{
    QwAudioAuAdpcmInfo info;
    uint64_t total_bits;
    if (!sample_count || !data_size) return QW_STATUS_ERR_FORMAT;
    if (!qw_audio_au_adpcm_describe(encoding, &info))
        return QW_STATUS_ERR_UNSUPPORTED;
    if (channels != 1u) return QW_STATUS_ERR_UNSUPPORTED;
    if ((uint64_t)data_size > UINT64_MAX / 8u) return QW_STATUS_ERR_FORMAT;
    total_bits = (uint64_t)data_size * 8u;
    *sample_count = total_bits / info.bits_per_sample;
    return *sample_count ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_au_adpcm_packet_sample_count(const QwAudioAuAdpcmInfo *info,
                                          size_t data_size,
                                          uint32_t *sample_count)
{
    uint64_t total_bits;
    uint64_t samples;
    if (!info || !sample_count || !data_size || !info->bits_per_sample)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if ((uint64_t)data_size > UINT64_MAX / 8u) return QW_STATUS_ERR_FORMAT;
    total_bits = (uint64_t)data_size * 8u;
    samples = total_bits / info->bits_per_sample;
    if (!samples || samples > UINT32_MAX) return QW_STATUS_ERR_FORMAT;
    *sample_count = (uint32_t)samples;
    return QW_STATUS_OK;
}
