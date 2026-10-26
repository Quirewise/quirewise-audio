#include "audio_mpeg_parser.h"

#include "../../core/byte_utils.h"
#include "audio_id3_tag.h"
#include "quirewise/status.h"

#include <string.h>

static const uint16_t mpeg_bitrates_kbps[2][3][16] = {
    {
        {0u, 32u, 64u, 96u, 128u, 160u, 192u, 224u,
         256u, 288u, 320u, 352u, 384u, 416u, 448u, 0u},
        {0u, 32u, 48u, 56u, 64u, 80u, 96u, 112u,
         128u, 160u, 192u, 224u, 256u, 320u, 384u, 0u},
        {0u, 32u, 40u, 48u, 56u, 64u, 80u, 96u,
         112u, 128u, 160u, 192u, 224u, 256u, 320u, 0u}
    },
    {
        {0u, 32u, 48u, 56u, 64u, 80u, 96u, 112u,
         128u, 144u, 160u, 176u, 192u, 224u, 256u, 0u},
        {0u, 8u, 16u, 24u, 32u, 40u, 48u, 56u,
         64u, 80u, 96u, 112u, 128u, 144u, 160u, 0u},
        {0u, 8u, 16u, 24u, 32u, 40u, 48u, 56u,
         64u, 80u, 96u, 112u, 128u, 144u, 160u, 0u}
    }
};

typedef struct QwAudioMpegHeader {
    uint32_t sample_rate;
    uint32_t bitrate;
    uint32_t frame_size;
    uint32_t sample_count;
    uint16_t channels;
    uint8_t layer;
    uint8_t version_id;
} QwAudioMpegHeader;

static int mpeg_parse_header(const uint8_t *bytes,
                             size_t size,
                             QwAudioMpegHeader *parsed)
{
    static const uint32_t sample_rates[3] = {44100u, 48000u, 32000u};
    uint32_t header;
    uint8_t version_id;
    uint8_t layer_bits;
    uint8_t sample_rate_index;
    uint8_t bitrate_index;
    uint8_t padding;
    uint8_t mode;
    uint8_t lsf;
    uint8_t mpeg25;
    uint32_t bitrate_kbps;
    uint32_t sample_rate;
    uint32_t frame_size;
    size_t header_size;

    if (!bytes || !parsed) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size < 4u) return QW_STATUS_ERR_FORMAT;
    header = ((uint32_t)bytes[0] << 24u) |
             ((uint32_t)bytes[1] << 16u) |
             ((uint32_t)bytes[2] << 8u) |
             (uint32_t)bytes[3];
    if ((header & UINT32_C(0xffe00000)) != UINT32_C(0xffe00000))
        return QW_STATUS_ERR_FORMAT;
    version_id = (uint8_t)((header >> 19u) & 0x03u);
    layer_bits = (uint8_t)((header >> 17u) & 0x03u);
    sample_rate_index = (uint8_t)((header >> 10u) & 0x03u);
    bitrate_index = (uint8_t)((header >> 12u) & 0x0fu);
    padding = (uint8_t)((header >> 9u) & 0x01u);
    mode = (uint8_t)((header >> 6u) & 0x03u);
    if (version_id == 1u || layer_bits == 0u ||
        sample_rate_index >= 3u || bitrate_index == 0u ||
        bitrate_index == 15u || (header & 0x03u) == 0x02u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    parsed->layer = (uint8_t)(4u - layer_bits);
    lsf = version_id == 3u ? 0u : 1u;
    mpeg25 = version_id == 0u ? 1u : 0u;
    sample_rate = sample_rates[sample_rate_index] >> (lsf + mpeg25);
    bitrate_kbps =
        mpeg_bitrates_kbps[lsf][parsed->layer - 1u][bitrate_index];
    if (!bitrate_kbps || !sample_rate) return QW_STATUS_ERR_UNSUPPORTED;

    if (parsed->layer == 1u) {
        frame_size = ((bitrate_kbps * 12000u) / sample_rate + padding) * 4u;
        parsed->sample_count = 384u;
    } else if (parsed->layer == 2u) {
        frame_size = (bitrate_kbps * 144000u) / sample_rate + padding;
        parsed->sample_count = 1152u;
    } else {
        frame_size = (bitrate_kbps * 144000u) /
                     (sample_rate << lsf) + padding;
        parsed->sample_count = lsf ? 576u : 1152u;
    }
    header_size = ((header >> 16u) & 0x01u) != 0u ? 4u : 6u;
    if (frame_size <= header_size || frame_size > size)
        return QW_STATUS_ERR_FORMAT;

    parsed->sample_rate = sample_rate;
    parsed->bitrate = bitrate_kbps * 1000u;
    parsed->frame_size = frame_size;
    parsed->channels = mode == 3u ? 1u : 2u;
    parsed->version_id = version_id;
    return QW_STATUS_OK;
}

int qw_audio_mpeg_probe(const uint8_t *bytes,
                        size_t size,
                        QwInputFormat input_format)
{
    QwAudioMpegHeader header = {0};
    size_t position = 0u;
    uint8_t expected_layer;

    if (!bytes) return 0;
    if (input_format == QW_FORMAT_MP2) {
        expected_layer = 2u;
    } else if (input_format == QW_FORMAT_MP3) {
        expected_layer = 3u;
    } else {
        return 0;
    }
    if (!qw_audio_id3_skip_leading_v2(bytes, size, &position) ||
        position > size ||
        mpeg_parse_header(bytes + position, size - position, &header) != QW_STATUS_OK) {
        return 0;
    }
    return header.layer == expected_layer;
}

int qw_audio_mpeg_next_frame(const QwAudioMpegSource *source,
                             size_t position,
                             QwAudioMpegFrame *frame)
{
    QwAudioMpegHeader header = {0};
    size_t remaining;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;
    remaining = source->frame_data_end - position;
    rc = mpeg_parse_header(source->data + position, remaining, &header);
    if (rc != QW_STATUS_OK) return rc;
    if (header.sample_rate != source->sample_rate ||
        header.channels != source->channels ||
        header.layer != source->layer ||
        header.version_id != source->version_id) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->data = source->data + position;
    frame->size = header.frame_size;
    frame->next_position = position + header.frame_size;
    frame->sample_count = header.sample_count;
    frame->bitrate = header.bitrate;
    return 1;
}

static int mpeg_validate_frame_range(QwAudioMpegSource *source,
                                     uint8_t expected_layer)
{
    QwAudioMpegHeader first = {0};
    QwAudioMpegFrame frame;
    size_t position;
    int rc;

    if (!source->data || source->first_frame_offset >= source->frame_data_end ||
        source->frame_data_end > source->size) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = mpeg_parse_header(source->data + source->first_frame_offset,
                           source->frame_data_end - source->first_frame_offset,
                           &first);
    if (rc != QW_STATUS_OK) return rc;
    if (first.layer != expected_layer) return QW_STATUS_ERR_UNSUPPORTED;
    source->sample_rate = first.sample_rate;
    source->bitrate = first.bitrate;
    source->channels = first.channels;
    source->layer = first.layer;
    source->version_id = first.version_id;

    position = source->first_frame_offset;
    while (position < source->frame_data_end) {
        rc = qw_audio_mpeg_next_frame(source, position, &frame);
        if (rc == QW_STATUS_ERR_FORMAT && position != source->first_frame_offset)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (source->sample_count > UINT64_MAX - frame.sample_count ||
            source->frame_count == SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        source->sample_count += frame.sample_count;
        ++source->frame_count;
        position = frame.next_position;
    }
    if (position != source->frame_data_end || !source->frame_count ||
        !source->sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_mpeg_parse_frames(const uint8_t *bytes,
                               size_t size,
                               QwInputFormat input_format,
                               QwAudioMpegSource *source,
                               QwConvertReport *report)
{
    uint8_t expected_layer;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (input_format == QW_FORMAT_MP2) {
        expected_layer = 2u;
    } else if (input_format == QW_FORMAT_MP3) {
        expected_layer = 3u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return qw_audio_mpeg_parse_layer_frames(bytes, size, expected_layer,
                                            source, report);
}

int qw_audio_mpeg_parse_layer_frames(const uint8_t *bytes,
                                     size_t size,
                                     uint8_t expected_layer,
                                     QwAudioMpegSource *source,
                                     QwConvertReport *report)
{
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (expected_layer < 1u || expected_layer > 3u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    source->data = bytes;
    source->size = size;
    source->frame_data_end = size;
    return mpeg_validate_frame_range(source, expected_layer);
}

int qw_audio_mpeg_parse(const uint8_t *bytes,
                        size_t size,
                        QwInputFormat input_format,
                        QwAudioMpegSource *source,
                        QwConvertReport *report)
{
    size_t position;
    int trailing_id3v1;
    uint8_t expected_layer;
    int rc;

    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (input_format == QW_FORMAT_MP2) {
        expected_layer = 2u;
    } else if (input_format == QW_FORMAT_MP3) {
        expected_layer = 3u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!qw_audio_id3_skip_leading_v2(bytes, size, &position))
        return QW_STATUS_ERR_FORMAT;
    if (position > size || size - position < 4u) return QW_STATUS_ERR_FORMAT;
    source->data = bytes;
    source->size = size;
    source->first_frame_offset = position;
    source->frame_data_end =
        qw_audio_id3v1_audio_end(bytes, size, &trailing_id3v1);
    source->has_metadata = position != 0u || trailing_id3v1;
    rc = mpeg_validate_frame_range(source, expected_layer);
    return rc;
}
