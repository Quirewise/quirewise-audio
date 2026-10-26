#include "audio_dolby_digital_internal.h"

#include "quirewise/status.h"

static const uint8_t eac3_block_counts[4] = {1u, 2u, 3u, 6u};

int qw_audio_eac3_parse_frame_header(
    const uint8_t *bytes,
    size_t size,
    uint32_t bitstream_id,
    QwAudioDolbyDigitalFrameHeader *header)
{
    size_t bit_position = 16u;
    uint32_t frame_type;
    uint32_t substream_id;
    uint32_t frame_size_code;
    uint32_t sample_rate_code;
    uint32_t channel_mode;
    uint32_t lfe_on;
    uint32_t blocks = 6u;
    uint32_t base_sample_rate;
    uint32_t sample_rate;
    uint32_t value;
    int rc;

    if (!bytes || !header || bitstream_id <= 10u || bitstream_id > 16u ||
        size < 6u) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                          &frame_type);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 3u,
                                          &substream_id);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 11u,
                                          &frame_size_code);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                          &sample_rate_code);
    if (rc != QW_STATUS_OK) return rc;
    if (frame_type == 3u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (sample_rate_code == 3u) {
        uint32_t half_rate_code;
        rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                              &half_rate_code);
        if (rc != QW_STATUS_OK || half_rate_code >= 3u)
            return QW_STATUS_ERR_UNSUPPORTED;
        base_sample_rate =
            qw_audio_dolby_digital_sample_rate(half_rate_code);
        sample_rate = base_sample_rate / 2u;
    } else {
        uint32_t block_code;
        rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 2u,
                                              &block_code);
        if (rc != QW_STATUS_OK) return rc;
        base_sample_rate =
            qw_audio_dolby_digital_sample_rate(sample_rate_code);
        sample_rate = base_sample_rate;
        blocks = eac3_block_counts[block_code];
    }
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 3u,
                                          &channel_mode);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 1u,
                                          &lfe_on);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dolby_digital_read_bits(bytes, size, &bit_position, 5u,
                                          &value);
    if (rc != QW_STATUS_OK || value != bitstream_id) return QW_STATUS_ERR_FORMAT;

    header->frame_size = (frame_size_code + 1u) * 2u;
    if (header->frame_size < 7u) return QW_STATUS_ERR_UNSUPPORTED;
    header->sample_rate = sample_rate;
    header->channels = qw_audio_dolby_digital_channel_count(channel_mode,
                                                             lfe_on);
    header->sample_count = blocks * 256u;
    header->codec = QW_AUDIO_DOLBY_DIGITAL_CODEC_EAC3;
    header->frame_type = (uint8_t)frame_type;
    header->substream_id = (uint8_t)substream_id;
    return QW_STATUS_OK;
}
