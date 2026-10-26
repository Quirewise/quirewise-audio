#include "audio_matroska_acm_parser.h"

#include "../../core/byte_utils.h"
#include "audio_wave_format.h"
#include "audio_wave_ima_format.h"
#include "audio_wave_mpeg_format.h"
#include "audio_wave_mpeg1_format.h"
#include "audio_wave_ms_format.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    MATROSKA_ACM_WAVEFORMATEX_SIZE = 18u,
    MATROSKA_ACM_PCM = 0x0001u,
    MATROSKA_ACM_MS_ADPCM = 0x0002u,
    MATROSKA_ACM_IEEE_FLOAT = 0x0003u,
    MATROSKA_ACM_ALAW = 0x0006u,
    MATROSKA_ACM_MULAW = 0x0007u,
    MATROSKA_ACM_IMA_ADPCM = 0x0011u,
    MATROSKA_ACM_MPEG = 0x0050u,
    MATROSKA_ACM_MPEG_LAYER_3 = 0x0055u
};

static int matroska_acm_parse_pcm(const uint8_t *bytes,
                                  size_t size,
                                  uint16_t extra_size,
                                  QwAudioMatroskaAcmFormat *format,
                                  QwConvertReport *report)
{
    QwAudioWavePcmFormat pcm;
    int rc;

    if (extra_size != 0u) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_wave_parse_pcm_format(bytes, size, 1, &pcm, report);
    if (rc != QW_STATUS_OK) return rc;
    format->sample_rate = pcm.sample_rate;
    format->channels = pcm.channels;
    format->bits_per_sample = pcm.sample_bits;
    format->block_align = pcm.block_align;
    format->channel_mask = pcm.channel_mask;
    if (pcm.floating_point) {
        format->codec = pcm.sample_bits == 32u
            ? QW_AUDIO_MATROSKA_CODEC_PCM_F32LE
            : QW_AUDIO_MATROSKA_CODEC_PCM_F64LE;
    } else {
        switch (pcm.sample_bits) {
        case 8u: format->codec = QW_AUDIO_MATROSKA_CODEC_PCM_U8; break;
        case 16u: format->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S16LE; break;
        case 24u: format->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S24LE; break;
        case 32u: format->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S32LE; break;
        default: return QW_STATUS_ERR_UNSUPPORTED;
        }
    }
    return QW_STATUS_OK;
}

static int matroska_acm_parse_ms_adpcm(const uint8_t *bytes,
                                       size_t size,
                                       size_t extra_size,
                                       QwAudioMatroskaAcmFormat *format,
                                       QwConvertReport *report)
{
    QwAudioWaveMsFormat ms;
    int rc = qw_audio_wave_ms_format_parse(bytes, size, &ms, report);

    if (rc != QW_STATUS_OK) return rc;
    if ((size_t)ms.extension_size != extra_size) return QW_STATUS_ERR_FORMAT;
    format->sample_rate = ms.sample_rate;
    format->channels = ms.channels;
    format->bits_per_sample = 16u;
    format->block_align = ms.block_align;
    format->wave_ms_format = ms;
    format->codec = QW_AUDIO_MATROSKA_CODEC_ADPCM_MS;
    return QW_STATUS_OK;
}

static int matroska_acm_parse_ima_adpcm(const uint8_t *bytes,
                                        size_t size,
                                        size_t extra_size,
                                        QwAudioMatroskaAcmFormat *format,
                                        QwConvertReport *report)
{
    QwAudioWaveImaFormat ima;
    int rc = qw_audio_wave_ima_format_parse(bytes, size, &ima, report);

    if (rc != QW_STATUS_OK) return rc;
    if ((size_t)ima.extension_size != extra_size) return QW_STATUS_ERR_FORMAT;
    format->sample_rate = ima.sample_rate;
    format->channels = ima.channels;
    format->bits_per_sample = 16u;
    format->block_align = ima.block_align;
    format->codec = QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV;
    return QW_STATUS_OK;
}

static int matroska_acm_parse_g711(const uint8_t *bytes,
                                   uint16_t format_tag,
                                   uint16_t extra_size,
                                   QwAudioMatroskaAcmFormat *format)
{
    uint16_t channels = qw_read_u16_le(bytes + 2u);
    uint32_t sample_rate = qw_read_u32_le(bytes + 4u);
    uint32_t byte_rate = qw_read_u32_le(bytes + 8u);
    uint16_t block_align = qw_read_u16_le(bytes + 12u);
    uint16_t bits_per_sample = qw_read_u16_le(bytes + 14u);
    uint64_t expected_byte_rate;

    if ((format_tag != MATROSKA_ACM_ALAW &&
         format_tag != MATROSKA_ACM_MULAW) ||
        !channels || channels > 8u || !sample_rate || sample_rate > 1000000u ||
        bits_per_sample != 8u || block_align != channels || extra_size != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    expected_byte_rate = (uint64_t)sample_rate * channels;
    if (expected_byte_rate > UINT32_MAX ||
        byte_rate != (uint32_t)expected_byte_rate) {
        return QW_STATUS_ERR_FORMAT;
    }
    format->sample_rate = sample_rate;
    format->channels = channels;
    format->bits_per_sample = bits_per_sample;
    format->block_align = block_align;
    format->codec = format_tag == MATROSKA_ACM_ALAW
        ? QW_AUDIO_MATROSKA_CODEC_PCM_ALAW
        : QW_AUDIO_MATROSKA_CODEC_PCM_MULAW;
    return QW_STATUS_OK;
}

static int matroska_acm_parse_mp3(const uint8_t *bytes,
                                 size_t size,
                                 size_t extra_size,
                                 QwAudioMatroskaAcmFormat *format,
                                 QwConvertReport *report)
{
    QwAudioWaveMpegFormat mpeg;
    int rc = qw_audio_wave_mpeg_format_parse(bytes, size, 1, &mpeg, report);

    if (rc != QW_STATUS_OK) return rc;
    if ((size_t)(size - MATROSKA_ACM_WAVEFORMATEX_SIZE) != extra_size)
        return QW_STATUS_ERR_FORMAT;
    format->sample_rate = mpeg.sample_rate;
    format->channels = mpeg.channels;
    format->bits_per_sample = mpeg.bits_per_sample;
    format->block_align = mpeg.block_align;
    format->codec = QW_AUDIO_MATROSKA_CODEC_MP3;
    format->codec_delay_ns =
        (uint64_t)mpeg.codec_delay_samples * UINT64_C(1000000000) /
        mpeg.sample_rate;
    return QW_STATUS_OK;
}

static int matroska_acm_parse_mpeg1(const uint8_t *bytes,
                                   size_t size,
                                   size_t extra_size,
                                   QwAudioMatroskaAcmFormat *format,
                                   QwConvertReport *report)
{
    QwAudioWaveMpeg1Format mpeg;
    int rc = qw_audio_wave_mpeg1_format_parse(bytes, size, 1, &mpeg, report);

    if (rc != QW_STATUS_OK) return rc;
    if (size < MATROSKA_ACM_WAVEFORMATEX_SIZE ||
        size - MATROSKA_ACM_WAVEFORMATEX_SIZE != extra_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    format->sample_rate = mpeg.sample_rate;
    format->mpeg1_bitrate = mpeg.bitrate;
    format->channels = mpeg.channels;
    format->block_align = mpeg.block_align;
    format->codec = mpeg.layer == 1u ? QW_AUDIO_MATROSKA_CODEC_MP1
        : mpeg.layer == 2u ? QW_AUDIO_MATROSKA_CODEC_MP2
                           : QW_AUDIO_MATROSKA_CODEC_MP3;
    format->is_mpeg1_acm_format = 1;
    return QW_STATUS_OK;
}

int qw_audio_matroska_acm_parse(const uint8_t *bytes,
                                size_t size,
                                QwAudioMatroskaAcmFormat *format,
                                QwConvertReport *report)
{
    uint16_t format_tag;
    uint16_t extra_size;

    if (!bytes || !format) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(format, 0, sizeof(*format));
    if (size < MATROSKA_ACM_WAVEFORMATEX_SIZE) return QW_STATUS_ERR_FORMAT;
    format_tag = qw_read_u16_le(bytes);
    extra_size = qw_read_u16_le(bytes + 16u);
    if ((size_t)extra_size != size - MATROSKA_ACM_WAVEFORMATEX_SIZE)
        return QW_STATUS_ERR_FORMAT;

    switch (format_tag) {
    case MATROSKA_ACM_PCM:
    case MATROSKA_ACM_IEEE_FLOAT:
        return matroska_acm_parse_pcm(bytes, size, extra_size, format, report);
    case MATROSKA_ACM_MS_ADPCM:
        return matroska_acm_parse_ms_adpcm(
            bytes, size, extra_size, format, report);
    case MATROSKA_ACM_ALAW:
    case MATROSKA_ACM_MULAW:
        return matroska_acm_parse_g711(bytes, format_tag, extra_size, format);
    case MATROSKA_ACM_IMA_ADPCM:
        return matroska_acm_parse_ima_adpcm(
            bytes, size, extra_size, format, report);
    case MATROSKA_ACM_MPEG:
        return matroska_acm_parse_mpeg1(
            bytes, size, extra_size, format, report);
    case MATROSKA_ACM_MPEG_LAYER_3:
        return matroska_acm_parse_mp3(bytes, size, extra_size, format, report);
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}
