#include "audio_matroska_parser.h"
#include "audio_matroska_acm_parser.h"
#include "audio_wavpack_matroska_parser.h"
#include "audio_matroska_additions_parser.h"
#include "audio_matroska_addition_mapping_parser.h"
#include "audio_matroska_ebml_parser.h"

#include "quirewise/status.h"

#include <limits.h>
#include <string.h>

#define MATROSKA_ID_EBML UINT32_C(0x1a45dfa3)
#define MATROSKA_ID_SEGMENT UINT32_C(0x18538067)
#define MATROSKA_ID_DOCTYPE UINT32_C(0x4282)
#define MATROSKA_ID_INFO UINT32_C(0x1549a966)
#define MATROSKA_ID_TIMECODE_SCALE UINT32_C(0x2ad7b1)
#define MATROSKA_ID_DURATION UINT32_C(0x4489)
#define MATROSKA_ID_TRACKS UINT32_C(0x1654ae6b)
#define MATROSKA_ID_TRACK_ENTRY UINT32_C(0xae)
#define MATROSKA_ID_TRACK_NUMBER UINT32_C(0xd7)
#define MATROSKA_ID_FLAG_DEFAULT UINT32_C(0x88)
#define MATROSKA_ID_TRACK_TYPE UINT32_C(0x83)
#define MATROSKA_ID_BLOCK_ADDITION_MAPPING UINT32_C(0x41e4)
#define MATROSKA_ID_CODEC_ID UINT32_C(0x86)
#define MATROSKA_ID_CODEC_PRIVATE UINT32_C(0x63a2)
#define MATROSKA_ID_CODEC_DELAY UINT32_C(0x56aa)
#define MATROSKA_ID_AUDIO UINT32_C(0xe1)
#define MATROSKA_ID_SAMPLING_FREQUENCY UINT32_C(0xb5)
#define MATROSKA_ID_OUTPUT_SAMPLING_FREQUENCY UINT32_C(0x78b5)
#define MATROSKA_ID_CHANNELS UINT32_C(0x9f)
#define MATROSKA_ID_BIT_DEPTH UINT32_C(0x6264)
#define MATROSKA_ID_CLUSTER UINT32_C(0x1f43b675)
#define MATROSKA_ID_CLUSTER_TIMECODE UINT32_C(0xe7)
#define MATROSKA_ID_SIMPLE_BLOCK UINT32_C(0xa3)
#define MATROSKA_ID_BLOCK_GROUP UINT32_C(0xa0)
#define MATROSKA_ID_BLOCK UINT32_C(0xa1)
#define MATROSKA_ID_BLOCK_ADDITIONS UINT32_C(0x75a1)
#define MATROSKA_ID_DISCARD_PADDING UINT32_C(0x75a2)

typedef QwAudioMatroskaEbmlElement MatroskaElement;

typedef struct MatroskaTrack {
    uint64_t number;
    uint64_t type;
    int default_enabled;
    int has_opaque_block_addition_mapping;
    uint32_t sample_rate;
    uint32_t output_sample_rate;
    uint32_t coded_sample_rate;
    uint32_t mpeg1_bitrate;
    uint32_t channel_mask;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint16_t block_align;
    QwAudioWaveMsFormat wave_ms_format;
    int has_sample_rate;
    int has_output_sample_rate;
    int has_channels;
    int has_bits_per_sample;
    uint64_t codec_delay_ns;
    int has_codec_delay;
    int is_mpeg1_acm_format;
    uint64_t sample_count;
    QwAudioMatroskaCodec codec;
    const uint8_t *codec_private;
    size_t codec_private_size;
    uint8_t generated_codec_private[10];
    size_t generated_codec_private_size;
    uint8_t flac_streaminfo[34];
} MatroskaTrack;

static int matroska_read_vint(const uint8_t *data,
                              size_t size,
                              size_t *position,
                              int keep_marker,
                              uint64_t *value,
                              unsigned *length_out)
{
    return qw_audio_matroska_ebml_read_vint(data, size, position,
                                            keep_marker, value, length_out);
}

static int matroska_read_element(const uint8_t *data,
                                 size_t parent_end,
                                 size_t position,
                                 MatroskaElement *element)
{
    return qw_audio_matroska_ebml_read_element(data, parent_end,
                                               position, element);
}

static int matroska_read_uint(const uint8_t *data,
                              size_t size,
                              uint64_t *value)
{
    return qw_audio_matroska_ebml_read_uint(data, size, value);
}

static int matroska_read_sint(const uint8_t *data,
                              size_t size,
                              int64_t *value)
{
    uint64_t raw;
    uint64_t magnitude;
    int rc;
    if (!value || size == 0u || size > 8u) return QW_STATUS_ERR_FORMAT;
    rc = matroska_read_uint(data, size, &raw);
    if (rc != QW_STATUS_OK) return rc;
    if (size < 8u && (data[0] & 0x80u) != 0u)
        raw |= UINT64_MAX << (size * 8u);
    if ((raw & (UINT64_C(1) << 63u)) == 0u) {
        *value = (int64_t)raw;
        return QW_STATUS_OK;
    }
    magnitude = (~raw) + 1u;
    if (magnitude == (UINT64_C(1) << 63u)) {
        *value = INT64_MIN;
    } else {
        *value = -(int64_t)magnitude;
    }
    return QW_STATUS_OK;
}

static uint16_t matroska_read_u16_le(const uint8_t *data)
{
    return (uint16_t)(data[0] | ((uint16_t)data[1] << 8u));
}

static uint32_t matroska_read_u32_le(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) | ((uint32_t)data[3] << 24u);
}

static uint32_t matroska_tta_crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    size_t index;
    for (index = 0u; index < size; ++index) {
        unsigned bit;
        crc ^= data[index];
        for (bit = 0u; bit < 8u; ++bit) {
            uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (UINT32_C(0xedb88320) & mask);
        }
    }
    return crc ^ UINT32_MAX;
}

static int matroska_parse_flac_private(const uint8_t *data,
                                       size_t size,
                                       MatroskaTrack *track)
{
    size_t position = 4u;
    int found_streaminfo = 0;
    int found_last = 0;
    unsigned block_count = 0u;
    if (!data || !track || size < 42u || memcmp(data, "fLaC", 4u) != 0)
        return QW_STATUS_ERR_FORMAT;
    while (position < size && block_count < 128u) {
        uint8_t header;
        uint8_t block_type;
        size_t block_size;
        if (size - position < 4u) return QW_STATUS_ERR_FORMAT;
        header = data[position++];
        block_type = header & 0x7fu;
        block_size = ((size_t)data[position] << 16u) |
                     ((size_t)data[position + 1u] << 8u) |
                     data[position + 2u];
        position += 3u;
        if (block_size > size - position || block_type == 127u)
            return QW_STATUS_ERR_FORMAT;
        if (!block_count && (block_type != 0u || block_size != 34u))
            return QW_STATUS_ERR_FORMAT;
        if (block_type == 0u) {
            if (block_size != 34u || found_streaminfo) return QW_STATUS_ERR_FORMAT;
            memcpy(track->flac_streaminfo, data + position, 34u);
            found_streaminfo = 1;
        }
        position += block_size;
        ++block_count;
        if (header & 0x80u) {
            found_last = 1;
            if (position != size) return QW_STATUS_ERR_FORMAT;
            break;
        }
    }
    if (!found_streaminfo || !found_last || position != size)
        return QW_STATUS_ERR_FORMAT;
    {
        uint64_t packed = 0u;
        unsigned index;
        uint32_t sample_rate;
        uint16_t channels;
        uint16_t bits_per_sample;
        uint64_t sample_count;
        for (index = 10u; index < 18u; ++index) {
            packed = (packed << 8u) | track->flac_streaminfo[index];
        }
        sample_rate = (uint32_t)((packed >> 44u) & UINT64_C(0xfffff));
        channels = (uint16_t)(((packed >> 41u) & 7u) + 1u);
        bits_per_sample = (uint16_t)(((packed >> 36u) & 31u) + 1u);
        sample_count = packed & UINT64_C(0xfffffffff);
        if (!sample_rate || !channels || bits_per_sample < 4u ||
            bits_per_sample > 32u ||
            (track->sample_rate && track->sample_rate != sample_rate) ||
            (track->channels && track->channels != channels) ||
            (track->bits_per_sample &&
             track->bits_per_sample != bits_per_sample)) {
            return QW_STATUS_ERR_FORMAT;
        }
        track->sample_rate = sample_rate;
        track->channels = channels;
        track->bits_per_sample = bits_per_sample;
        track->sample_count = sample_count;
    }
    return QW_STATUS_OK;
}

static int matroska_read_float_value(const uint8_t *data,
                                     size_t size,
                                     double *value)
{
    uint32_t float_bits;
    uint64_t double_bits;
    float float_value;
    double double_value;
    if (!data || !value) return QW_STATUS_ERR_FORMAT;
    if (size == 4u) {
        float_bits = ((uint32_t)data[0] << 24u) |
                     ((uint32_t)data[1] << 16u) |
                     ((uint32_t)data[2] << 8u) | data[3];
        memcpy(&float_value, &float_bits, sizeof(float_value));
        *value = (double)float_value;
        return *value > 0.0 ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    if (size == 8u) {
        if (matroska_read_uint(data, size, &double_bits) != QW_STATUS_OK)
            return QW_STATUS_ERR_FORMAT;
        memcpy(&double_value, &double_bits, sizeof(double_value));
        *value = double_value;
        return *value > 0.0 ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_ERR_FORMAT;
}

static int matroska_read_sample_rate(const uint8_t *data,
                                     size_t size,
                                     uint32_t *value)
{
    double sample_rate;
    int rc = matroska_read_float_value(data, size, &sample_rate);
    if (rc != QW_STATUS_OK || sample_rate > 1000000.0)
        return QW_STATUS_ERR_FORMAT;
    *value = (uint32_t)(sample_rate + 0.5);
    return *value ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static QwAudioMatroskaCodec matroska_codec_from_id(const uint8_t *data,
                                                   size_t size)
{
#define MATROSKA_CODEC_ID(text, value) \
    (size == sizeof(text) - 1u && memcmp(data, text, sizeof(text) - 1u) == 0 \
         ? value : QW_AUDIO_MATROSKA_CODEC_NONE)
    QwAudioMatroskaCodec codec;
    if (!data) return QW_AUDIO_MATROSKA_CODEC_NONE;
    if (size > sizeof("A_AAC/") - 1u &&
        memcmp(data, "A_AAC/", sizeof("A_AAC/") - 1u) == 0)
        return QW_AUDIO_MATROSKA_CODEC_AAC;
    if (size == sizeof("A_DTS/EXPRESS") - 1u &&
        memcmp(data, "A_DTS/EXPRESS", sizeof("A_DTS/EXPRESS") - 1u) == 0)
        return QW_AUDIO_MATROSKA_CODEC_DTS;
    codec = MATROSKA_CODEC_ID("A_AAC", QW_AUDIO_MATROSKA_CODEC_AAC);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_OPUS", QW_AUDIO_MATROSKA_CODEC_OPUS);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_OPUS/EXPERIMENTAL",
                              QW_AUDIO_MATROSKA_CODEC_OPUS);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_VORBIS", QW_AUDIO_MATROSKA_CODEC_VORBIS);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_FLAC", QW_AUDIO_MATROSKA_CODEC_FLAC);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_ALAC", QW_AUDIO_MATROSKA_CODEC_ALAC);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_ATRAC/AT1", QW_AUDIO_MATROSKA_CODEC_ATRAC1);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_MPEG/L2", QW_AUDIO_MATROSKA_CODEC_MP2);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_MPEG/L1", QW_AUDIO_MATROSKA_CODEC_MP1);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_MPEG/L3", QW_AUDIO_MATROSKA_CODEC_MP3);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_AC3", QW_AUDIO_MATROSKA_CODEC_AC3);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_AC3/BSID9", QW_AUDIO_MATROSKA_CODEC_AC3);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_AC3/BSID10", QW_AUDIO_MATROSKA_CODEC_AC3);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_EAC3", QW_AUDIO_MATROSKA_CODEC_EAC3);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_DTS", QW_AUDIO_MATROSKA_CODEC_DTS);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_DTS/LOSSLESS", QW_AUDIO_MATROSKA_CODEC_DTS);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_QUICKTIME/QDMC", QW_AUDIO_MATROSKA_CODEC_QDMC);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_QUICKTIME/QDM2", QW_AUDIO_MATROSKA_CODEC_QDM2);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_TTA1", QW_AUDIO_MATROSKA_CODEC_TTA);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_WAVPACK4",
                              QW_AUDIO_MATROSKA_CODEC_WAVPACK);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_MLP", QW_AUDIO_MATROSKA_CODEC_MLP);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_TRUEHD", QW_AUDIO_MATROSKA_CODEC_TRUEHD);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_PCM/INT/LIT", QW_AUDIO_MATROSKA_CODEC_PCM_S8);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_PCM/INT/BIG", QW_AUDIO_MATROSKA_CODEC_PCM_S8);
    if (codec) return codec;
    codec = MATROSKA_CODEC_ID("A_PCM/FLOAT/IEEE", QW_AUDIO_MATROSKA_CODEC_PCM_F32LE);
    if (codec) return codec;
#undef MATROSKA_CODEC_ID
    return QW_AUDIO_MATROSKA_CODEC_NONE;
}

static uint8_t matroska_aac_legacy_object_type(const uint8_t *data,
                                              size_t size,
                                              int *is_sbr)
{
#define MATROSKA_AAC_PROFILE(text, value) \
    (size == sizeof(text) - 1u && memcmp(data, text, sizeof(text) - 1u) == 0 \
         ? value : 0u)
    uint8_t object_type;
    if (is_sbr) *is_sbr = 0;
    if (!data) return 0u;
    if (size == sizeof("A_AAC/MPEG2/LC/SBR") - 1u &&
        memcmp(data, "A_AAC/MPEG2/LC/SBR", size) == 0) {
        if (is_sbr) *is_sbr = 1;
        return 2u;
    }
    if (size == sizeof("A_AAC/MPEG4/LC/SBR") - 1u &&
        memcmp(data, "A_AAC/MPEG4/LC/SBR", size) == 0) {
        if (is_sbr) *is_sbr = 1;
        return 2u;
    }
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG2/MAIN", 1u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG2/LC", 2u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG2/SSR", 3u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG4/MAIN", 1u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG4/LC", 2u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG4/SSR", 3u);
    if (object_type) return object_type;
    object_type = MATROSKA_AAC_PROFILE("A_AAC/MPEG4/LTP", 4u);
#undef MATROSKA_AAC_PROFILE
    return object_type;
}

static void matroska_write_bits(uint8_t *output,
                                size_t *bit_position,
                                uint32_t value,
                                unsigned bit_count)
{
    unsigned remaining;
    for (remaining = bit_count; remaining > 0u; --remaining) {
        unsigned output_bit = (value >> (remaining - 1u)) & 1u;
        size_t position = (*bit_position)++;
        output[position / 8u] |=
            (uint8_t)(output_bit << (7u - (unsigned)(position % 8u)));
    }
}

static int matroska_write_aac_frequency(uint8_t *output,
                                        size_t *bit_position,
                                        uint32_t sample_rate)
{
    static const uint32_t sample_rates[13] = {
        96000u, 88200u, 64000u, 48000u, 44100u, 32000u, 24000u,
        22050u, 16000u, 12000u, 11025u, 8000u, 7350u
    };
    uint32_t frequency_index = 15u;
    size_t index;

    if (!output || !bit_position || !sample_rate ||
        sample_rate > 0x00ffffffu) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    for (index = 0u; index < sizeof(sample_rates) / sizeof(sample_rates[0]);
         ++index) {
        if (sample_rates[index] == sample_rate) {
            frequency_index = (uint32_t)index;
            break;
        }
    }
    matroska_write_bits(output, bit_position, frequency_index, 4u);
    if (frequency_index == 15u)
        matroska_write_bits(output, bit_position, sample_rate, 24u);
    return QW_STATUS_OK;
}

static int matroska_build_aac_specific_config(uint8_t object_type,
                                              int is_sbr,
                                              uint32_t core_sample_rate,
                                              uint32_t output_sample_rate,
                                              uint16_t channels,
                                              uint8_t output[10],
                                              size_t *output_size)
{
    uint32_t channel_config;
    size_t bit_position = 0u;

    if (!output || !output_size || object_type < 1u || object_type > 4u ||
        !output_sample_rate || output_sample_rate > 0x00ffffffu ||
        (is_sbr && (!core_sample_rate ||
                    core_sample_rate >= output_sample_rate))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (channels >= 1u && channels <= 6u) channel_config = channels;
    else if (channels == 8u) channel_config = 7u;
    else return QW_STATUS_ERR_UNSUPPORTED;

    memset(output, 0, 10u);
    if (is_sbr) {
        matroska_write_bits(output, &bit_position, 5u, 5u);
        if (matroska_write_aac_frequency(output, &bit_position,
                                         core_sample_rate) != QW_STATUS_OK) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        matroska_write_bits(output, &bit_position, channel_config, 4u);
        if (matroska_write_aac_frequency(output, &bit_position,
                                         output_sample_rate) != QW_STATUS_OK) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        matroska_write_bits(output, &bit_position, object_type, 5u);
    } else {
        matroska_write_bits(output, &bit_position, object_type, 5u);
        if (matroska_write_aac_frequency(output, &bit_position,
                                         output_sample_rate) != QW_STATUS_OK) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        matroska_write_bits(output, &bit_position, channel_config, 4u);
    }
    matroska_write_bits(output, &bit_position, 0u, 3u);
    *output_size = (bit_position + 7u) / 8u;
    return QW_STATUS_OK;
}

static void matroska_select_pcm_variant(MatroskaTrack *track,
                                        const uint8_t *codec_id,
                                        size_t codec_id_size)
{
    if (!track || !codec_id) return;
    if (codec_id_size == sizeof("A_PCM/INT/LIT") - 1u &&
        memcmp(codec_id, "A_PCM/INT/LIT", codec_id_size) == 0) {
        switch (track->bits_per_sample) {
        case 8u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_U8; break;
        case 16u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S16LE; break;
        case 24u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S24LE; break;
        case 32u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S32LE; break;
        default: track->codec = QW_AUDIO_MATROSKA_CODEC_NONE; break;
        }
    } else if (codec_id_size == sizeof("A_PCM/INT/BIG") - 1u &&
               memcmp(codec_id, "A_PCM/INT/BIG", codec_id_size) == 0) {
        switch (track->bits_per_sample) {
        case 8u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_U8; break;
        case 16u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S16BE; break;
        case 24u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S24BE; break;
        case 32u: track->codec = QW_AUDIO_MATROSKA_CODEC_PCM_S32BE; break;
        default: track->codec = QW_AUDIO_MATROSKA_CODEC_NONE; break;
        }
    } else if (codec_id_size == sizeof("A_PCM/FLOAT/IEEE") - 1u &&
               memcmp(codec_id, "A_PCM/FLOAT/IEEE", codec_id_size) == 0) {
        track->codec = track->bits_per_sample == 32u
            ? QW_AUDIO_MATROSKA_CODEC_PCM_F32LE
            : track->bits_per_sample == 64u
                ? QW_AUDIO_MATROSKA_CODEC_PCM_F64LE
                : QW_AUDIO_MATROSKA_CODEC_NONE;
    }
}

static int matroska_parse_audio_settings(const uint8_t *data,
                                         size_t end,
                                         size_t position,
                                         MatroskaTrack *track)
{
    MatroskaElement element;
    while (position < end) {
        uint64_t value;
        int rc = matroska_read_element(data, end, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_SAMPLING_FREQUENCY ||
            element.id == MATROSKA_ID_OUTPUT_SAMPLING_FREQUENCY) {
            uint32_t sample_rate;
            rc = matroska_read_sample_rate(data + element.payload,
                                           element.size,
                                           &sample_rate);
            if (rc != QW_STATUS_OK) return rc;
            if (element.id == MATROSKA_ID_OUTPUT_SAMPLING_FREQUENCY) {
                track->output_sample_rate = sample_rate;
                track->has_output_sample_rate = 1;
            } else {
                track->sample_rate = sample_rate;
                track->has_sample_rate = 1;
            }
        } else if (element.id == MATROSKA_ID_CHANNELS) {
            rc = matroska_read_uint(data + element.payload, element.size,
                                    &value);
            if (rc != QW_STATUS_OK || value == 0u || value > 64u)
                return QW_STATUS_ERR_FORMAT;
            track->channels = (uint16_t)value;
            track->has_channels = 1;
        } else if (element.id == MATROSKA_ID_BIT_DEPTH) {
            rc = matroska_read_uint(data + element.payload, element.size,
                                    &value);
            if (rc != QW_STATUS_OK || value > UINT16_MAX)
                return QW_STATUS_ERR_FORMAT;
            track->bits_per_sample = (uint16_t)value;
            track->has_bits_per_sample = 1;
        }
        position = element.next;
    }
    return position == end ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int matroska_parse_wavpack_private(MatroskaTrack *track)
{
    uint16_t version = 0x0410u;

    if (!track || !track->sample_rate || !track->channels)
        return QW_STATUS_ERR_FORMAT;
    if (track->channels > QW_AUDIO_WAVPACK_MATROSKA_MAX_BLOCKS)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (track->has_output_sample_rate &&
        track->output_sample_rate != track->sample_rate) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (track->bits_per_sample > 32u) return QW_STATUS_ERR_UNSUPPORTED;
    if (track->codec_private_size) {
        if (!track->codec_private || track->codec_private_size != 2u)
            return QW_STATUS_ERR_FORMAT;
        version = matroska_read_u16_le(track->codec_private);
    }
    if (version < 0x0402u || version > 0x0410u)
        return QW_STATUS_ERR_UNSUPPORTED;

    track->generated_codec_private[0] = (uint8_t)version;
    track->generated_codec_private[1] = (uint8_t)(version >> 8u);
    track->generated_codec_private_size = 2u;
    track->codec_private = track->generated_codec_private;
    track->codec_private_size = 2u;
    return QW_STATUS_OK;
}

static int matroska_parse_acm_private(MatroskaTrack *track)
{
    QwAudioMatroskaAcmFormat format;
    int bits_match;
    int sample_rate_is_fixed;
    int rc;

    if (!track || !track->codec_private) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_matroska_acm_parse(track->codec_private,
                                     track->codec_private_size,
                                     &format, NULL);
    if (rc != QW_STATUS_OK) return rc;

    sample_rate_is_fixed = format.sample_rate != 0u;
    if (!sample_rate_is_fixed && format.is_mpeg1_acm_format) {
        if (!track->sample_rate) return QW_STATUS_ERR_UNSUPPORTED;
        format.sample_rate = track->sample_rate;
    }
    if (!format.sample_rate) return QW_STATUS_ERR_FORMAT;

    bits_match = !track->has_bits_per_sample ||
                 track->bits_per_sample == format.bits_per_sample;
    if (track->has_bits_per_sample &&
        (format.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_MS ||
         format.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV)) {
        bits_match = track->bits_per_sample == 4u ||
                     track->bits_per_sample == 16u;
    }
    if ((sample_rate_is_fixed && track->has_sample_rate &&
         track->sample_rate != format.sample_rate) ||
        (sample_rate_is_fixed && track->has_output_sample_rate &&
         track->output_sample_rate != format.sample_rate) ||
        (track->has_channels && track->channels != format.channels) ||
        !bits_match) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!track->has_codec_delay && format.codec_delay_ns) {
        if (format.codec_delay_ns > UINT64_C(60000000000))
            return QW_STATUS_ERR_FORMAT;
        track->codec_delay_ns = format.codec_delay_ns;
    }

    track->sample_rate = format.sample_rate;
    track->mpeg1_bitrate = format.mpeg1_bitrate;
    track->channels = format.channels;
    track->bits_per_sample = format.bits_per_sample;
    track->block_align = format.block_align;
    track->channel_mask = format.channel_mask;
    track->wave_ms_format = format.wave_ms_format;
    track->is_mpeg1_acm_format = format.is_mpeg1_acm_format;
    track->codec = format.codec;
    track->codec_private = NULL;
    track->codec_private_size = 0u;
    return QW_STATUS_OK;
}

static int matroska_parse_track_entry(const uint8_t *data,
                                      const MatroskaElement *entry,
                                      MatroskaTrack *track)
{
    MatroskaElement element;
    size_t position;
    const uint8_t *codec_id_data = NULL;
    size_t codec_id_size = 0u;
    uint32_t codec_sample_rate;
    int rc;
    memset(track, 0, sizeof(*track));
    track->channels = 1u;
    track->default_enabled = 1;
    position = entry->payload;
    while (position < entry->next) {
        uint64_t value;
        rc = matroska_read_element(data, entry->next, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        switch (element.id) {
        case MATROSKA_ID_TRACK_NUMBER:
        case MATROSKA_ID_TRACK_TYPE:
            rc = matroska_read_uint(data + element.payload, element.size,
                                    &value);
            if (rc != QW_STATUS_OK || value == 0u) return QW_STATUS_ERR_FORMAT;
            if (element.id == MATROSKA_ID_TRACK_NUMBER) track->number = value;
            else track->type = value;
            break;
        case MATROSKA_ID_FLAG_DEFAULT:
            rc = matroska_read_uint(data + element.payload, element.size,
                                    &value);
            if (rc != QW_STATUS_OK || value > 1u) return QW_STATUS_ERR_FORMAT;
            track->default_enabled = value != 0u;
            break;
        case MATROSKA_ID_CODEC_ID:
            codec_id_data = data + element.payload;
            codec_id_size = element.size;
            break;
        case MATROSKA_ID_CODEC_PRIVATE:
            track->codec_private = data + element.payload;
            track->codec_private_size = element.size;
            break;
        case MATROSKA_ID_CODEC_DELAY:
            rc = matroska_read_uint(data + element.payload, element.size,
                                    &track->codec_delay_ns);
            if (rc != QW_STATUS_OK || track->codec_delay_ns > UINT64_C(60000000000))
                return QW_STATUS_ERR_FORMAT;
            track->has_codec_delay = 1;
            break;
        case MATROSKA_ID_BLOCK_ADDITION_MAPPING: {
            QwAudioMatroskaAdditionMapping mapping;
            rc = qw_audio_matroska_addition_mapping_parse(
                data + element.payload, element.size, &mapping);
            if (rc != QW_STATUS_OK) return rc;
            /* BlockAddID 1 is codec-defined; mapped IDs start at 2. */
            if (mapping.block_add_id_type == 1u &&
                !mapping.has_block_add_id_value) {
                if (track->has_opaque_block_addition_mapping)
                    return QW_STATUS_ERR_FORMAT;
                track->has_opaque_block_addition_mapping = 1;
            }
            break;
        }
        case MATROSKA_ID_AUDIO:
            rc = matroska_parse_audio_settings(data, element.next,
                                               element.payload, track);
            if (rc != QW_STATUS_OK) return rc;
            break;
        default:
            break;
        }
        position = element.next;
    }
    if (track->type != 2u) return QW_STATUS_OK;
    if (!track->number || !codec_id_data) return QW_STATUS_ERR_FORMAT;
    codec_sample_rate = track->sample_rate;
    track->codec = matroska_codec_from_id(codec_id_data, codec_id_size);
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_NONE &&
        codec_id_size == sizeof("A_MS/ACM") - 1u &&
        memcmp(codec_id_data, "A_MS/ACM", codec_id_size) == 0) {
        rc = matroska_parse_acm_private(track);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_NONE)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_WAVPACK) {
        rc = matroska_parse_wavpack_private(track);
        if (rc != QW_STATUS_OK) return rc;
    }
    track->coded_sample_rate = track->sample_rate;
    if (track->output_sample_rate)
        track->sample_rate = track->output_sample_rate;
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S8) {
        matroska_select_pcm_variant(track, codec_id_data, codec_id_size);
    } else if (track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_F32LE) {
        if (codec_id_size == sizeof("A_PCM/FLOAT/IEEE") - 1u &&
            memcmp(codec_id_data, "A_PCM/FLOAT/IEEE", codec_id_size) == 0) {
            track->codec = track->bits_per_sample == 32u
                ? QW_AUDIO_MATROSKA_CODEC_PCM_F32LE
                : track->bits_per_sample == 64u
                    ? QW_AUDIO_MATROSKA_CODEC_PCM_F64LE
                    : QW_AUDIO_MATROSKA_CODEC_NONE;
        }
    }
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_OPUS && !track->sample_rate)
        track->sample_rate = 48000u;
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_AAC &&
        !track->codec_private_size) {
        int is_sbr;
        uint8_t object_type = matroska_aac_legacy_object_type(
            codec_id_data, codec_id_size, &is_sbr);
        rc = matroska_build_aac_specific_config(object_type, is_sbr,
                                                codec_sample_rate,
                                                track->sample_rate,
                                                track->channels,
                                                track->generated_codec_private,
                                                &track->generated_codec_private_size);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_FLAC) {
        rc = matroska_parse_flac_private(track->codec_private,
                                         track->codec_private_size, track);
        if (rc != QW_STATUS_OK) return rc;
    } else if (track->codec == QW_AUDIO_MATROSKA_CODEC_ALAC) {
        uint64_t cookie_rate;
        uint8_t cookie_channels;
        uint8_t cookie_bits;
        if (!track->codec_private || track->codec_private_size < 36u ||
            memcmp(track->codec_private + 4u, "alac", 4u) != 0 ||
            matroska_read_uint(track->codec_private + 32u, 4u,
                               &cookie_rate) != QW_STATUS_OK ||
            cookie_rate == 0u || cookie_rate > 1000000u) {
            return QW_STATUS_ERR_FORMAT;
        }
        cookie_bits = track->codec_private[17u];
        cookie_channels = track->codec_private[21u];
        if (!cookie_channels || cookie_channels > 64u || !cookie_bits ||
            cookie_bits > 32u ||
            (track->sample_rate && track->sample_rate != cookie_rate) ||
            (track->channels && track->channels != cookie_channels) ||
            (track->bits_per_sample &&
             track->bits_per_sample != cookie_bits)) {
            return QW_STATUS_ERR_FORMAT;
        }
        track->sample_rate = (uint32_t)cookie_rate;
        track->channels = cookie_channels;
        track->bits_per_sample = cookie_bits;
    }
    if (!track->sample_rate || !track->channels) return QW_STATUS_ERR_FORMAT;
    if ((track->codec == QW_AUDIO_MATROSKA_CODEC_AAC &&
         !track->codec_private_size && !track->generated_codec_private_size) ||
        ((track->codec == QW_AUDIO_MATROSKA_CODEC_OPUS ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_VORBIS ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_FLAC ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_QDMC ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_QDM2) &&
         !track->codec_private_size)) return QW_STATUS_ERR_FORMAT;
    if (track->codec == QW_AUDIO_MATROSKA_CODEC_TTA) {
        const uint8_t *header = track->codec_private;
        uint16_t format;
        uint16_t header_channels;
        uint16_t header_bits;
        uint32_t header_rate;
        uint32_t header_samples;
        if (!header || track->codec_private_size != 22u ||
            memcmp(header, "TTA1", 4u) != 0) return QW_STATUS_ERR_FORMAT;
        format = matroska_read_u16_le(header + 4u);
        header_channels = matroska_read_u16_le(header + 6u);
        header_bits = matroska_read_u16_le(header + 8u);
        header_rate = matroska_read_u32_le(header + 10u);
        header_samples = matroska_read_u32_le(header + 14u);
        if (format != 1u || !header_samples || header_bits == 0u ||
            header_bits > 24u || header_channels != track->channels ||
            header_bits != track->bits_per_sample ||
            header_rate != track->sample_rate ||
            matroska_tta_crc32(header, 18u) !=
                matroska_read_u32_le(header + 18u)) {
            return QW_STATUS_ERR_FORMAT;
        }
        track->sample_count = header_samples;
    }
    if ((track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_U8 ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S8 ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S16LE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S16BE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S24LE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S24BE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S32LE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_S32BE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_F32LE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_F32BE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_F64LE ||
         track->codec == QW_AUDIO_MATROSKA_CODEC_PCM_F64BE) &&
        !track->bits_per_sample) return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}

static int matroska_parse_tracks(const uint8_t *data,
                                 const MatroskaElement *tracks,
                                 size_t requested_track,
                                 QwAudioMatroskaSource *source)
{
    MatroskaElement entry;
    size_t position = tracks->payload;
    size_t audio_ordinal = 0u;
    int selected_unsupported = 0;
    while (position < tracks->next) {
        MatroskaTrack track;
        int rc = matroska_read_element(data, tracks->next, position, &entry);
        if (rc != QW_STATUS_OK || entry.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (entry.id == MATROSKA_ID_TRACK_ENTRY) {
            rc = matroska_parse_track_entry(data, &entry, &track);
            if (rc != QW_STATUS_OK && rc != QW_STATUS_ERR_UNSUPPORTED) {
                return rc;
            }
            if (track.type == 2u) {
                int supported = rc == QW_STATUS_OK;
                int should_select;
                ++audio_ordinal;
                if (requested_track) {
                    should_select = requested_track == audio_ordinal;
                } else {
                    should_select = !source->track_number ||
                        (track.default_enabled &&
                         !source->selected_track_is_default);
                }
                if (should_select) {
                    selected_unsupported = !supported;
                    source->track_number = track.number;
                    source->codec = supported
                        ? track.codec : QW_AUDIO_MATROSKA_CODEC_NONE;
                    source->sample_rate = supported ? track.sample_rate : 0u;
                    source->channels = supported ? track.channels : 0u;
                    source->coded_sample_rate =
                        supported ? track.coded_sample_rate : 0u;
                    source->mpeg1_bitrate =
                        supported ? track.mpeg1_bitrate : 0u;
                    source->bits_per_sample =
                        supported ? track.bits_per_sample : 0u;
                    source->codec_private = supported
                        ? track.codec_private : NULL;
                    source->codec_private_size =
                        supported ? track.codec_private_size : 0u;
                    source->has_opaque_block_addition_mapping =
                        supported && track.has_opaque_block_addition_mapping;
                    source->is_mpeg1_acm_format =
                        supported && track.is_mpeg1_acm_format;
                    if (supported && track.generated_codec_private_size) {
                        memcpy(source->generated_codec_private,
                               track.generated_codec_private,
                               track.generated_codec_private_size);
                        source->codec_private = source->generated_codec_private;
                        source->codec_private_size =
                            track.generated_codec_private_size;
                    }
                    source->codec_delay_ns =
                        supported ? track.codec_delay_ns : 0u;
                    source->sample_count =
                        supported ? track.sample_count : 0u;
                    source->channel_mask =
                        supported ? track.channel_mask : 0u;
                    source->block_align =
                        supported ? track.block_align : 0u;
                    if (supported &&
                        track.codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_MS) {
                        source->wave_ms_format = track.wave_ms_format;
                    }
                    if (supported &&
                        track.codec == QW_AUDIO_MATROSKA_CODEC_FLAC) {
                        memcpy(source->flac_streaminfo,
                               track.flac_streaminfo,
                               sizeof(source->flac_streaminfo));
                    }
                    source->selected_track_is_default =
                        requested_track == 0u && track.default_enabled;
                }
            }
        }
        position = entry.next;
    }
    source->audio_track_count = audio_ordinal;
    if (!audio_ordinal) return QW_STATUS_ERR_UNSUPPORTED;
    if (requested_track > audio_ordinal) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (selected_unsupported) return QW_STATUS_ERR_UNSUPPORTED;
    return source->track_number ? QW_STATUS_OK : QW_STATUS_ERR_UNSUPPORTED;
}

static int matroska_parse_info(const uint8_t *data,
                               const MatroskaElement *info,
                               uint64_t *timecode_scale,
                               uint64_t *duration_us)
{
    MatroskaElement element;
    size_t position = info->payload;
    double duration_ticks = 0.0;
    while (position < info->next) {
        int rc = matroska_read_element(data, info->next, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_TIMECODE_SCALE) {
            rc = matroska_read_uint(data + element.payload, element.size,
                                    timecode_scale);
            if (rc != QW_STATUS_OK || !*timecode_scale ||
                *timecode_scale > UINT64_C(1000000000)) {
                return QW_STATUS_ERR_FORMAT;
            }
        } else if (element.id == MATROSKA_ID_DURATION) {
            rc = matroska_read_float_value(data + element.payload,
                                           element.size, &duration_ticks);
            if (rc != QW_STATUS_OK) return rc;
        }
        position = element.next;
    }
    if (duration_ticks > 0.0) {
        double duration = duration_ticks * (double)*timecode_scale / 1000.0;
        if (duration >= (double)INT64_MAX) return QW_STATUS_ERR_FORMAT;
        *duration_us = (uint64_t)duration;
    }
    return QW_STATUS_OK;
}

static int matroska_header_doctype(const uint8_t *data,
                                   size_t end,
                                   const MatroskaElement *header)
{
    MatroskaElement element;
    size_t position = header->payload;
    while (position < header->next) {
        int rc = matroska_read_element(data, header->next, position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return 0;
        if (element.id == MATROSKA_ID_DOCTYPE) {
            const uint8_t *text = data + element.payload;
            if (element.size == sizeof("matroska") - 1u &&
                memcmp(text, "matroska", sizeof("matroska") - 1u) == 0)
                return 1;
            if (element.size == 4u && memcmp(text, "webm", 4u) == 0)
                return 2;
            return 0;
        }
        position = element.next;
    }
    (void)end;
    return 0;
}

int qw_audio_matroska_parse(const uint8_t *data,
                            size_t size,
                            size_t requested_audio_track,
                            QwAudioMatroskaSource *source)
{
    MatroskaElement root;
    MatroskaElement element;
    size_t position = 0u;
    size_t segment_payload = 0u;
    size_t segment_end = 0u;
    int found_header = 0;
    int found_segment = 0;
    int found_tracks = 0;
    int doctype_kind = 0;
    int rc;

    if (!data || !size || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    source->data = data;
    source->size = size;
    source->timecode_scale_ns = UINT64_C(1000000);
    while (position < size) {
        rc = matroska_read_element(data, size, position, &root);
        if (rc != QW_STATUS_OK) return found_header ? QW_STATUS_ERR_FORMAT : QW_STATUS_ERR_UNSUPPORTED;
        if (root.id == MATROSKA_ID_EBML) {
            if (root.unknown_size ||
                !(doctype_kind = matroska_header_doctype(data, size, &root)))
                return QW_STATUS_ERR_UNSUPPORTED;
            found_header = 1;
        } else if (root.id == MATROSKA_ID_SEGMENT) {
            segment_payload = root.payload;
            segment_end = root.next;
            found_segment = 1;
            break;
        }
        position = root.next;
    }
    if (!found_header || !found_segment || segment_payload >= segment_end)
        return QW_STATUS_ERR_UNSUPPORTED;
    source->segment_end = segment_end;
    source->is_webm = doctype_kind == 2;
    position = segment_payload;
    while (position < segment_end) {
        rc = matroska_read_element(data, segment_end, position, &element);
        if (rc != QW_STATUS_OK) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_INFO) {
            rc = matroska_parse_info(data, &element,
                                     &source->timecode_scale_ns,
                                     &source->duration_us);
            if (rc != QW_STATUS_OK) return rc;
        } else if (element.id == MATROSKA_ID_TRACKS) {
            rc = matroska_parse_tracks(data, &element,
                                       requested_audio_track, source);
            if (rc != QW_STATUS_OK) return rc;
            found_tracks = 1;
        } else if (element.id == MATROSKA_ID_CLUSTER &&
                   source->next_cluster_position == 0u) {
            source->next_cluster_position = position;
        }
        if (element.next <= position) return QW_STATUS_ERR_FORMAT;
        position = element.next;
    }
    if (!found_tracks || !source->next_cluster_position)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (!source->track_number) return QW_STATUS_ERR_UNSUPPORTED;
    return QW_STATUS_OK;
}

static int matroska_parse_cluster_timecode(QwAudioMatroskaSource *source)
{
    MatroskaElement cluster_child;
    size_t position = source->cluster_payload_position;
    source->cluster_timecode = 0u;
    while (position < source->cluster_end) {
        int rc = matroska_read_element(source->data, source->cluster_end,
                                      position, &cluster_child);
        if (rc != QW_STATUS_OK || cluster_child.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (cluster_child.id == MATROSKA_ID_CLUSTER_TIMECODE) {
            rc = matroska_read_uint(source->data + cluster_child.payload,
                                    cluster_child.size,
                                    &source->cluster_timecode);
            return rc;
        }
        position = cluster_child.next;
    }
    return QW_STATUS_ERR_FORMAT;
}

static int matroska_read_block_track(const uint8_t *data,
                                    size_t size,
                                    size_t *position,
                                    uint64_t *track_number)
{
    return matroska_read_vint(data, size, position, 0, track_number, NULL);
}

static int matroska_prepare_block(QwAudioMatroskaSource *source,
                                  const MatroskaElement *block)
{
    const uint8_t *data = source->data;
    size_t position = block->payload;
    size_t end = block->next;
    uint64_t track_number;
    uint8_t flags;
    unsigned lacing;
    uint8_t lace_byte;
    size_t frame_data_offset;
    size_t frame_data_size;
    size_t frame_count;
    size_t index;
    int rc;

    source->block_discard_padding_ns = 0;
    source->has_block_additions = 0;
    memset(&source->block_additions, 0, sizeof(source->block_additions));
    if (block->unknown_size || block->size < 4u) return QW_STATUS_ERR_FORMAT;
    rc = matroska_read_block_track(data, end, &position, &track_number);
    if (rc != QW_STATUS_OK || end - position < 3u) return QW_STATUS_ERR_FORMAT;
    if (track_number != source->track_number) return 0;
    flags = data[position + 2u];
    position += 3u;
    lacing = (flags >> 1u) & 3u;
    frame_data_offset = position;
    frame_data_size = end - position;
    if (lacing == 0u) {
        frame_count = 1u;
        source->block_frame_sizes[0] = frame_data_size;
        source->block_frame_offsets[0] = 0u;
    } else {
        size_t cursor;
        if (!frame_data_size) return QW_STATUS_ERR_FORMAT;
        lace_byte = data[position++];
        frame_count = (size_t)lace_byte + 1u;
        if (!frame_count || frame_count > 256u) return QW_STATUS_ERR_FORMAT;
        if (lacing == 1u) {
            for (index = 0u; index + 1u < frame_count; ++index) {
                size_t lace_size = 0u;
                uint8_t value;
                do {
                    if (position >= end) return QW_STATUS_ERR_FORMAT;
                    value = data[position++];
                    if (lace_size > SIZE_MAX - value) return QW_STATUS_ERR_FORMAT;
                    lace_size += value;
                } while (value == 255u);
                source->block_frame_sizes[index] = lace_size;
            }
        } else if (lacing == 2u) {
            size_t payload = end - position;
            if (!frame_count || payload % frame_count != 0u)
                return QW_STATUS_ERR_FORMAT;
            for (index = 0u; index < frame_count; ++index)
                source->block_frame_sizes[index] = payload / frame_count;
        } else {
            uint64_t first_size;
            unsigned vint_length;
            rc = matroska_read_vint(data, end, &position, 0,
                                    &first_size, &vint_length);
            if (rc != QW_STATUS_OK || first_size > SIZE_MAX) return QW_STATUS_ERR_FORMAT;
            if (frame_count == 1u &&
                first_size != (uint64_t)(end - position)) {
                return QW_STATUS_ERR_FORMAT;
            }
            source->block_frame_sizes[0] = (size_t)first_size;
            for (index = 1u; index + 1u < frame_count; ++index) {
                uint64_t encoded_delta;
                int64_t delta;
                uint64_t bias;
                rc = matroska_read_vint(data, end, &position, 0,
                                        &encoded_delta, &vint_length);
                if (rc != QW_STATUS_OK || vint_length > 8u) return QW_STATUS_ERR_FORMAT;
                bias = (UINT64_C(1) << (7u * vint_length - 1u)) - 1u;
                if (encoded_delta >= bias) {
                    uint64_t positive = encoded_delta - bias;
                    if (positive > INT64_MAX) return QW_STATUS_ERR_FORMAT;
                    delta = (int64_t)positive;
                } else {
                    uint64_t negative = bias - encoded_delta;
                    if (negative > (uint64_t)INT64_MAX) return QW_STATUS_ERR_FORMAT;
                    delta = -(int64_t)negative;
                }
                if (delta < 0 &&
                    (uint64_t)(-delta) > source->block_frame_sizes[index - 1u])
                    return QW_STATUS_ERR_FORMAT;
                if (delta > 0 &&
                    (uint64_t)delta > SIZE_MAX - source->block_frame_sizes[index - 1u])
                    return QW_STATUS_ERR_FORMAT;
                source->block_frame_sizes[index] =
                    delta < 0
                        ? source->block_frame_sizes[index - 1u] - (size_t)(-delta)
                        : source->block_frame_sizes[index - 1u] + (size_t)delta;
            }
        }
        cursor = position;
        for (index = 0u; index + 1u < frame_count; ++index) {
            size_t frame_size = source->block_frame_sizes[index];
            if (frame_size > end - cursor) return QW_STATUS_ERR_FORMAT;
            source->block_frame_offsets[index] = cursor - frame_data_offset;
            cursor += frame_size;
        }
        if (cursor > end) return QW_STATUS_ERR_FORMAT;
        source->block_frame_offsets[frame_count - 1u] = cursor - frame_data_offset;
        source->block_frame_sizes[frame_count - 1u] = end - cursor;
    }
    if (frame_data_offset > end || frame_count > UINT16_MAX) return QW_STATUS_ERR_FORMAT;
    source->block_frames = data + frame_data_offset;
    source->block_frames_size = frame_data_size;
    source->block_frame_count = (uint16_t)frame_count;
    source->block_frame_index = 0u;
    return 1;
}

static int matroska_find_block_in_group(QwAudioMatroskaSource *source,
                                        const MatroskaElement *group)
{
    MatroskaElement element;
    MatroskaElement block = {0};
    MatroskaElement additions = {0};
    QwAudioMatroskaBlockAdditions parsed_additions = {0};
    size_t position = group->payload;
    int block_result = 0;
    int found_block = 0;
    int has_additions = 0;
    int has_discard_padding = 0;
    int64_t discard_padding_ns = 0;
    while (position < group->next) {
        int rc = matroska_read_element(source->data, group->next,
                                      position, &element);
        if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
        if (element.id == MATROSKA_ID_BLOCK) {
            if (found_block) return QW_STATUS_ERR_FORMAT;
            found_block = 1;
            block = element;
        } else if (element.id == MATROSKA_ID_BLOCK_ADDITIONS) {
            if (has_additions) return QW_STATUS_ERR_FORMAT;
            has_additions = 1;
            additions = element;
        } else if (element.id == MATROSKA_ID_DISCARD_PADDING) {
            if (has_discard_padding) return QW_STATUS_ERR_FORMAT;
            has_discard_padding = 1;
            rc = matroska_read_sint(source->data + element.payload,
                                    element.size, &discard_padding_ns);
            if (rc != QW_STATUS_OK) return rc;
        }
        position = element.next;
    }
    if (position != group->next) return QW_STATUS_ERR_FORMAT;
    if (has_discard_padding && !found_block) return QW_STATUS_ERR_FORMAT;
    if (found_block) {
        block_result = matroska_prepare_block(source, &block);
        if (block_result < 0) return block_result;
    }
    if (block_result > 0) {
        source->block_discard_padding_ns = has_discard_padding
            ? discard_padding_ns : 0;
        if (has_additions) {
            int additions_rc;
            if (source->codec == QW_AUDIO_MATROSKA_CODEC_WAVPACK &&
                source->block_frame_count != 1u) {
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            additions_rc = qw_audio_matroska_additions_parse(
                source->data + additions.payload, additions.size,
                &parsed_additions);
            if (additions_rc != QW_STATUS_OK) return additions_rc;
            source->has_block_additions = 1;
            source->block_additions = parsed_additions;
        }
    }
    return block_result;
}

int qw_audio_matroska_next_packet(QwAudioMatroskaSource *source,
                                  const uint8_t **packet_data,
                                  size_t *packet_size,
                                  int64_t *discard_padding_ns,
                                  const QwAudioMatroskaBlockAdditions **block_additions)
{
    if (!source || !packet_data || !packet_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *packet_data = NULL;
    *packet_size = 0u;
    if (discard_padding_ns) *discard_padding_ns = 0;
    if (block_additions) *block_additions = NULL;
    for (;;) {
        if (source->block_frame_index < source->block_frame_count) {
            size_t index = source->block_frame_index++;
            size_t offset = source->block_frame_offsets[index];
            size_t size = source->block_frame_sizes[index];
            if (!size || offset > source->block_frames_size ||
                size > source->block_frames_size - offset) {
                return QW_STATUS_ERR_FORMAT;
            }
            *packet_data = source->block_frames + offset;
            *packet_size = size;
            if (block_additions && source->has_block_additions)
                *block_additions = &source->block_additions;
            if ((source->block_discard_padding_ns < 0 && index == 0u) ||
                (source->block_discard_padding_ns > 0 &&
                 index + 1u == source->block_frame_count)) {
                if (discard_padding_ns)
                    *discard_padding_ns = source->block_discard_padding_ns;
                source->block_discard_padding_ns = 0;
            }
            return 1;
        }
        if (source->cluster_loaded &&
            source->cluster_child_position < source->cluster_end) {
            MatroskaElement element;
            int rc = matroska_read_element(source->data, source->cluster_end,
                                           source->cluster_child_position,
                                           &element);
            if (rc != QW_STATUS_OK || element.unknown_size) return QW_STATUS_ERR_FORMAT;
            source->cluster_child_position = element.next;
            if (element.id == MATROSKA_ID_SIMPLE_BLOCK) {
                rc = matroska_prepare_block(source, &element);
                if (rc < 0) return rc;
                if (rc > 0) continue;
            } else if (element.id == MATROSKA_ID_BLOCK_GROUP) {
                rc = matroska_find_block_in_group(source, &element);
                if (rc < 0) return rc;
                if (rc > 0) continue;
            }
            continue;
        }
        source->cluster_loaded = 0;
        while (source->next_cluster_position < source->segment_end) {
            MatroskaElement element;
            int rc = matroska_read_element(source->data, source->segment_end,
                                           source->next_cluster_position,
                                           &element);
            if (rc != QW_STATUS_OK) return QW_STATUS_ERR_FORMAT;
            source->next_cluster_position = element.next;
            if (element.id != MATROSKA_ID_CLUSTER) continue;
            source->cluster_payload_position = element.payload;
            source->cluster_end = element.next;
            rc = matroska_parse_cluster_timecode(source);
            if (rc != QW_STATUS_OK) return rc;
            source->cluster_child_position = source->cluster_payload_position;
            source->cluster_loaded = 1;
            break;
        }
        if (!source->cluster_loaded) return 0;
    }
}
