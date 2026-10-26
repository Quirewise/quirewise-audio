#include "audio_aac_specific_config_parser.h"

#include "audio_aac_ld_specific_config_parser.h"
#include "audio_aac_pce_parser.h"
#include "quirewise/status.h"

#include <string.h>

typedef struct AudioAacBitReader {
    const uint8_t *data;
    size_t bit_size;
    size_t bit_position;
} AudioAacBitReader;

static int audio_aac_read_bits(AudioAacBitReader *reader,
                               unsigned bit_count,
                               uint32_t *value)
{
    uint32_t result = 0u;
    unsigned index;

    if (!reader || !value || bit_count > 32u ||
        reader->bit_position > reader->bit_size ||
        (size_t)bit_count > reader->bit_size - reader->bit_position) {
        return 0;
    }
    for (index = 0u; index < bit_count; ++index) {
        size_t position = reader->bit_position++;
        result = (result << 1u) |
            ((reader->data[position / 8u] >> (7u - position % 8u)) & 1u);
    }
    *value = result;
    return 1;
}

static int audio_aac_read_object_type(AudioAacBitReader *reader,
                                      uint32_t *object_type)
{
    uint32_t value;
    if (!audio_aac_read_bits(reader, 5u, &value)) return 0;
    if (value == 31u) {
        uint32_t escaped;
        if (!audio_aac_read_bits(reader, 6u, &escaped)) return 0;
        value = 32u + escaped;
    }
    *object_type = value;
    return 1;
}

static int audio_aac_read_sample_rate(AudioAacBitReader *reader,
                                      uint32_t *sample_rate,
                                      uint32_t *frequency_index)
{
    static const uint32_t sample_rates[13] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u, 7350u
    };
    if (!reader || !sample_rate || !frequency_index ||
        !audio_aac_read_bits(reader, 4u, frequency_index)) {
        return 0;
    }
    if (*frequency_index == 15u)
        return audio_aac_read_bits(reader, 24u, sample_rate) && *sample_rate;
    if (*frequency_index >= sizeof(sample_rates) / sizeof(sample_rates[0]))
        return 0;
    *sample_rate = sample_rates[*frequency_index];
    return 1;
}

static uint16_t audio_aac_channel_count(uint32_t configuration)
{
    static const uint8_t channels[8] = {0u, 1u, 2u, 3u, 4u, 5u, 6u, 8u};
    return configuration < sizeof(channels) / sizeof(channels[0])
        ? channels[configuration] : 0u;
}

static int audio_aac_is_supported_core_object_type(uint32_t object_type)
{
    return object_type >= 1u && object_type <= 4u;
}

static int audio_aac_parse_sync_extension(AudioAacBitReader *reader,
                                          uint32_t *sample_rate,
                                          uint8_t *sbr_signaled,
                                          uint8_t *sbr_present,
                                          uint8_t *ps_present)
{
    size_t saved_position;
    uint32_t sync_extension_type;
    uint32_t extension_object_type;
    uint32_t sbr_flag;

    if (!reader || !sample_rate || !sbr_signaled || !sbr_present ||
        !ps_present) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (reader->bit_position > reader->bit_size ||
        reader->bit_size - reader->bit_position < 16u) {
        return QW_STATUS_OK;
    }
    saved_position = reader->bit_position;
    if (!audio_aac_read_bits(reader, 11u, &sync_extension_type) ||
        sync_extension_type != 0x2b7u) {
        reader->bit_position = saved_position;
        return QW_STATUS_OK;
    }
    if (!audio_aac_read_object_type(reader, &extension_object_type) ||
        extension_object_type != 5u ||
        !audio_aac_read_bits(reader, 1u, &sbr_flag)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *sbr_signaled = 1u;
    *sbr_present = (uint8_t)(sbr_flag != 0u);
    if (!*sbr_present) return QW_STATUS_OK;
    {
        uint32_t frequency_index;
        if (!audio_aac_read_sample_rate(reader, sample_rate,
                                        &frequency_index)) {
            return QW_STATUS_ERR_FORMAT;
        }
    }
    if (reader->bit_position <= reader->bit_size &&
        reader->bit_size - reader->bit_position >= 12u) {
        size_t ps_position = reader->bit_position;
        uint32_t ps_extension_type;
        if (!audio_aac_read_bits(reader, 11u, &ps_extension_type))
            return QW_STATUS_ERR_FORMAT;
        if (ps_extension_type == 0x548u) {
            uint32_t ps_flag;
            if (!audio_aac_read_bits(reader, 1u, &ps_flag))
                return QW_STATUS_ERR_FORMAT;
            *ps_present = (uint8_t)(ps_flag != 0u);
        } else {
            reader->bit_position = ps_position;
        }
    }
    return QW_STATUS_OK;
}

static int audio_aac_parse_ga_specific_config(
    AudioAacBitReader *reader,
    uint32_t object_type,
    uint32_t channel_configuration,
    uint32_t frequency_index,
    uint16_t *channels,
    uint8_t *frame_length_flag)
{
    uint32_t depends_on_core_coder;
    uint32_t extension_flag;
    uint32_t frame_length_flag_value;
    uint32_t ignored_core_coder_delay;
    int rc;

    if (!reader || !channels || !frame_length_flag ||
        !audio_aac_is_supported_core_object_type(object_type)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!audio_aac_read_bits(reader, 1u, &frame_length_flag_value) ||
        !audio_aac_read_bits(reader, 1u, &depends_on_core_coder) ||
        (depends_on_core_coder &&
         !audio_aac_read_bits(reader, 14u, &ignored_core_coder_delay)) ||
        !audio_aac_read_bits(reader, 1u, &extension_flag)) {
        return QW_STATUS_ERR_FORMAT;
    }
    *frame_length_flag = (uint8_t)(frame_length_flag_value != 0u);
    if (extension_flag) return QW_STATUS_ERR_UNSUPPORTED;

    if (channel_configuration == 0u) {
        size_t pce_end_bit;
        rc = qw_audio_aac_pce_parse_syntax_at_bit_offset(
            reader->data, reader->bit_size / 8u, reader->bit_position,
            (uint8_t)(object_type - 1u), (uint8_t)frequency_index,
            channels, &pce_end_bit);
        if (rc != QW_STATUS_OK) return rc;
        reader->bit_position = pce_end_bit;
    } else {
        *channels = audio_aac_channel_count(channel_configuration);
        if (!*channels) return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

int qw_audio_aac_specific_config_parse(
    const uint8_t *config,
    size_t config_size,
    QwAudioAacSpecificConfig *parsed)
{
    AudioAacBitReader reader;
    uint32_t initial_object_type;
    uint32_t core_object_type;
    uint32_t initial_frequency_index;
    uint32_t extension_frequency_index;
    uint32_t core_sample_rate;
    uint32_t output_sample_rate;
    uint32_t channel_configuration;
    uint16_t core_channels = 0u;
    uint8_t frame_length_flag = 0u;
    uint8_t sbr_signaled = 0u;
    uint8_t sbr_present = 0u;
    uint8_t ps_present = 0u;
    uint32_t core_frames_per_packet;
    int rc;

    if (!config || !parsed || config_size < 2u ||
        config_size > QW_AUDIO_AAC_SPECIFIC_CONFIG_MAX_SIZE ||
        config_size > SIZE_MAX / 8u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    memset(parsed, 0, sizeof(*parsed));
    reader.data = config;
    reader.bit_size = config_size * 8u;
    reader.bit_position = 0u;
    if (!audio_aac_read_object_type(&reader, &initial_object_type) ||
        !audio_aac_read_sample_rate(&reader, &core_sample_rate,
                                    &initial_frequency_index) ||
        !audio_aac_read_bits(&reader, 4u, &channel_configuration)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    core_object_type = initial_object_type;
    output_sample_rate = core_sample_rate;
    if (initial_object_type == 5u || initial_object_type == 29u) {
        sbr_signaled = 1u;
        sbr_present = 1u;
        ps_present = (uint8_t)(initial_object_type == 29u);
        if (!audio_aac_read_sample_rate(&reader, &output_sample_rate,
                                        &extension_frequency_index) ||
            !audio_aac_read_object_type(&reader, &core_object_type) ||
            !audio_aac_is_supported_core_object_type(core_object_type)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
    } else if (initial_object_type != 23u &&
               !audio_aac_is_supported_core_object_type(core_object_type)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (core_object_type == 23u) {
        QwAudioAacLdSpecificConfig ld_config;
        size_t next_bit_offset;

        rc = qw_audio_aac_ld_specific_config_parse_at_bit_offset(
            reader.data, config_size, reader.bit_position,
            channel_configuration, initial_frequency_index,
            &ld_config, &next_bit_offset);
        if (rc != QW_STATUS_OK) return rc;
        reader.bit_position = next_bit_offset;
        core_channels = ld_config.channels;
        core_frames_per_packet = ld_config.frames_per_packet;
    } else {
        rc = audio_aac_parse_ga_specific_config(
            &reader, core_object_type, channel_configuration,
            initial_frequency_index, &core_channels, &frame_length_flag);
        if (rc != QW_STATUS_OK) return rc;
        core_frames_per_packet = frame_length_flag ? 960u : 1024u;
    }
    if (initial_object_type != 5u && initial_object_type != 29u &&
        initial_object_type != 23u) {
        rc = audio_aac_parse_sync_extension(
            &reader, &output_sample_rate, &sbr_signaled, &sbr_present,
            &ps_present);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (ps_present && core_channels != 1u) return QW_STATUS_ERR_UNSUPPORTED;

    parsed->core_sample_rate = core_sample_rate;
    parsed->output_sample_rate = output_sample_rate;
    parsed->core_frames_per_packet = core_frames_per_packet;
    parsed->output_frames_per_packet = parsed->core_frames_per_packet;
    if (sbr_present && core_sample_rate <= UINT32_MAX / 2u &&
        output_sample_rate == core_sample_rate * 2u) {
        parsed->output_frames_per_packet *= 2u;
    }
    parsed->core_channels = core_channels;
    parsed->output_channels = ps_present ? 2u : core_channels;
    parsed->object_type = (uint8_t)core_object_type;
    parsed->channel_configuration = (uint8_t)channel_configuration;
    parsed->sbr_signaled = sbr_signaled;
    parsed->sbr_present = sbr_present;
    parsed->ps_present = ps_present;
    return QW_STATUS_OK;
}
