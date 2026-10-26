#include "audio_dolby_digital_internal.h"

#include "quirewise/status.h"

#include <stdint.h>

static const uint16_t ac3_bitrates_kbps[19] = {
    32u, 40u, 48u, 56u, 64u, 80u, 96u, 112u, 128u, 160u,
    192u, 224u, 256u, 320u, 384u, 448u, 512u, 576u, 640u
};

int qw_audio_ac3_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    uint32_t bitstream_id,
    QwAudioDolbyDigitalFrameHeader *header)
{
    size_t bit_position = 16u;
    uint32_t value;
    uint32_t sample_rate_code;
    uint32_t frame_size_code;
    uint32_t bitstream_mode;
    uint32_t channel_mode;
    uint32_t lfe_on;
    uint32_t base_sample_rate;
    uint64_t frame_size;
    int rc;

    if (!bytes || !header || bitstream_id > 10u || size < 6u)
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 16u,
                                          &value);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                          &sample_rate_code);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 6u,
                                          &frame_size_code);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 5u,
                                          &value);
    if (rc != QW_STATUS_OK || value != bitstream_id) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 3u,
                                          &bitstream_mode);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 3u,
                                          &channel_mode);
    if (rc != QW_STATUS_OK) return rc;
    if (sample_rate_code >= 3u || frame_size_code > 37u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (channel_mode == 2u) {
        rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                              &value);
        if (rc != QW_STATUS_OK) return rc;
    } else {
        if ((channel_mode & 1u) != 0u && channel_mode != 1u) {
            rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position,
                                                  2u, &value);
            if (rc != QW_STATUS_OK) return rc;
        }
        if ((channel_mode & 4u) != 0u) {
            rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position,
                                                  2u, &value);
            if (rc != QW_STATUS_OK) return rc;
        }
    }
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 1u,
                                          &lfe_on);
    if (rc != QW_STATUS_OK) return rc;

    base_sample_rate = qw_audio_dolby_digital_sample_rate(sample_rate_code);
    frame_size = ((uint64_t)ac3_bitrates_kbps[frame_size_code >> 1u] *
                  1000u * 1536u) / ((uint64_t)base_sample_rate * 8u);
    frame_size &= ~UINT64_C(1);
    if (sample_rate_code == 1u && (frame_size_code & 1u) != 0u)
        frame_size += 2u;
    if (frame_size < 7u || frame_size > UINT32_MAX)
        return QW_STATUS_ERR_UNSUPPORTED;

    header->frame_size = (uint32_t)frame_size;
    header->sample_rate = base_sample_rate >>
        (bitstream_id > 8u ? bitstream_id - 8u : 0u);
    header->channels = qw_audio_dolby_digital_channel_count(channel_mode,
                                                             lfe_on);
    header->sample_count = 1536u;
    header->codec = QW_AUDIO_DOLBY_DIGITAL_CODEC_AC3;
    (void)bitstream_mode;
    return QW_STATUS_OK;
}
