#include "audio_wavpack_internal.h"

#include "core/byte_utils.h"
#include "quirewise/status.h"

const uint32_t audio_wavpack_sample_rates[16] = {
    6000u, 8000u, 9600u, 11025u, 12000u, 16000u, 22050u, 24000u,
    32000u, 44100u, 48000u, 64000u, 88200u, 96000u, 192000u, 0u
};

int audio_wavpack_parse_custom_sample_rate(const uint8_t *data,
                                           size_t size,
                                           uint32_t *sample_rate)
{
    size_t position = 0u;
    int found_sample_rate = 0;

    if (!data || !sample_rate) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *sample_rate = 0u;
    while (position < size) {
        uint8_t id;
        uint32_t word_count;
        size_t padded_data_size;
        size_t data_size;
        if (size - position < 2u) return QW_STATUS_ERR_FORMAT;
        id = data[position++];
        word_count = data[position++];
        if (id & AUDIO_WAVPACK_METADATA_LONG_FLAG) {
            if (size - position < 2u) return QW_STATUS_ERR_FORMAT;
            word_count |= (uint32_t)qw_read_u16_le(data + position) << 8u;
            position += 2u;
        }
        padded_data_size = (size_t)word_count * 2u;
        data_size = padded_data_size;
        if (id & AUDIO_WAVPACK_METADATA_ODD_FLAG) {
            if (!data_size) return QW_STATUS_ERR_FORMAT;
            --data_size;
        }
        if (padded_data_size > size - position) return QW_STATUS_ERR_FORMAT;
        if ((id & AUDIO_WAVPACK_METADATA_ID_MASK) ==
            AUDIO_WAVPACK_METADATA_SAMPLE_RATE) {
            uint32_t parsed_rate;
            if (found_sample_rate || data_size != 3u)
                return QW_STATUS_ERR_FORMAT;
            parsed_rate = (uint32_t)data[position] |
                          ((uint32_t)data[position + 1u] << 8u) |
                          ((uint32_t)data[position + 2u] << 16u);
            if (!parsed_rate) return QW_STATUS_ERR_FORMAT;
            *sample_rate = parsed_rate;
            found_sample_rate = 1;
        }
        position += padded_data_size;
    }
    return found_sample_rate ? QW_STATUS_OK : QW_STATUS_ERR_UNSUPPORTED;
}

int audio_wavpack_parse_dsd_rate_factor(const uint8_t *data,
                                        size_t size,
                                        uint32_t *rate_factor)
{
    size_t position = 0u;
    int found_dsd_data = 0;

    if (!data || !rate_factor) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *rate_factor = 0u;
    while (position < size) {
        uint8_t id;
        uint32_t word_count;
        size_t padded_data_size;
        size_t data_size;
        if (size - position < 2u) return QW_STATUS_ERR_FORMAT;
        id = data[position++];
        word_count = data[position++];
        if (id & AUDIO_WAVPACK_METADATA_LONG_FLAG) {
            if (size - position < 2u) return QW_STATUS_ERR_FORMAT;
            word_count |= (uint32_t)qw_read_u16_le(data + position) << 8u;
            position += 2u;
        }
        padded_data_size = (size_t)word_count * 2u;
        data_size = padded_data_size;
        if (id & AUDIO_WAVPACK_METADATA_ODD_FLAG) {
            if (!data_size) return QW_STATUS_ERR_FORMAT;
            --data_size;
        }
        if (padded_data_size > size - position) return QW_STATUS_ERR_FORMAT;
        if ((id & AUDIO_WAVPACK_METADATA_ID_MASK) ==
            AUDIO_WAVPACK_METADATA_DSD_DATA) {
            uint8_t rate_exponent;
            uint8_t dsd_mode;
            if (found_dsd_data || data_size <= 2u) return QW_STATUS_ERR_FORMAT;
            rate_exponent = data[position];
            dsd_mode = data[position + 1u];
            if (rate_exponent > 30u ||
                (dsd_mode != 0u && dsd_mode != 1u && dsd_mode != 3u)) {
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            *rate_factor = UINT32_C(1) << rate_exponent;
            found_dsd_data = 1;
        }
        position += padded_data_size;
    }
    return found_dsd_data ? QW_STATUS_OK : QW_STATUS_ERR_UNSUPPORTED;
}
