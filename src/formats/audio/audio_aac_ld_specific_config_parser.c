#include "audio_aac_ld_specific_config_parser.h"

#include "audio_aac_pce_parser.h"
#include "quirewise/status.h"

#include <string.h>

typedef struct AudioAacLdBitReader {
    const uint8_t *data;
    size_t bit_count;
    size_t bit_position;
} AudioAacLdBitReader;

static int audio_aac_ld_read_bits(AudioAacLdBitReader *reader,
                                  unsigned count,
                                  uint32_t *value)
{
    uint32_t result = 0u;
    unsigned index;

    if (!reader || !value || count > 32u ||
        reader->bit_position > reader->bit_count ||
        (size_t)count > reader->bit_count - reader->bit_position) {
        return 0;
    }
    for (index = 0u; index < count; ++index) {
        size_t position = reader->bit_position++;
        result = (result << 1u) |
            ((reader->data[position / 8u] >> (7u - position % 8u)) & 1u);
    }
    *value = result;
    return 1;
}

static uint16_t audio_aac_ld_channel_count(uint32_t configuration)
{
    static const uint8_t channels[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u};
    return configuration < sizeof(channels) / sizeof(channels[0])
        ? channels[configuration] : 0u;
}

int qw_audio_aac_ld_specific_config_parse_at_bit_offset(
    const uint8_t *config,
    size_t config_size,
    size_t bit_offset,
    uint32_t channel_configuration,
    uint32_t frequency_index,
    QwAudioAacLdSpecificConfig *parsed,
    size_t *next_bit_offset)
{
    AudioAacLdBitReader reader;
    uint32_t frame_length_flag;
    uint32_t depends_on_core_coder;
    uint32_t extension_flag;
    uint32_t ignored;
    uint32_t ep_config;
    uint32_t resilience_flags = 0u;
    uint32_t extension_flag3 = 0u;
    uint16_t channels;
    int rc;

    if (!config || !config_size || !parsed || config_size > SIZE_MAX / 8u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(parsed, 0, sizeof(*parsed));
    reader.data = config;
    reader.bit_count = config_size * 8u;
    if (bit_offset > reader.bit_count) return QW_STATUS_ERR_FORMAT;
    reader.bit_position = bit_offset;

    if (!audio_aac_ld_read_bits(&reader, 1u, &frame_length_flag) ||
        !audio_aac_ld_read_bits(&reader, 1u, &depends_on_core_coder)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (depends_on_core_coder &&
        !audio_aac_ld_read_bits(&reader, 14u, &ignored)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!audio_aac_ld_read_bits(&reader, 1u, &extension_flag))
        return QW_STATUS_ERR_FORMAT;

    if (channel_configuration == 0u) {
        size_t pce_end_bit;
        rc = qw_audio_aac_pce_parse_syntax_at_bit_offset(
            config, config_size, reader.bit_position,
            QW_AUDIO_AAC_PCE_ANY_OBJECT_TYPE, (uint8_t)frequency_index,
            &channels, &pce_end_bit);
        if (rc != QW_STATUS_OK) return rc;
        reader.bit_position = pce_end_bit;
    } else {
        channels = audio_aac_ld_channel_count(channel_configuration);
        if (!channels) return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (extension_flag) {
        if (!audio_aac_ld_read_bits(&reader, 3u, &resilience_flags) ||
            !audio_aac_ld_read_bits(&reader, 1u, &extension_flag3)) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (extension_flag3) return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (!audio_aac_ld_read_bits(&reader, 2u, &ep_config))
        return QW_STATUS_ERR_FORMAT;
    if (ep_config != 0u) return QW_STATUS_ERR_UNSUPPORTED;

    parsed->channels = channels;
    parsed->frames_per_packet = frame_length_flag ? 480u : 512u;
    parsed->resilience_flags = (uint8_t)resilience_flags;
    parsed->extension_flag3 = (uint8_t)extension_flag3;
    if (next_bit_offset) *next_bit_offset = reader.bit_position;
    return QW_STATUS_OK;
}
