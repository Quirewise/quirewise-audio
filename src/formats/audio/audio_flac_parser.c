#include "audio_flac_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    FLAC_MARKER_SIZE = 4u,
    FLAC_METADATA_HEADER_SIZE = 4u
};

typedef struct QwAudioFlacFrameHeader {
    uint32_t block_size;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    size_t header_size;
    uint8_t blocking_strategy;
} QwAudioFlacFrameHeader;

static const uint8_t flac_marker[FLAC_MARKER_SIZE] = {'f', 'L', 'a', 'C'};

static uint16_t flac_read_u16_be(const uint8_t *bytes)
{
    return (uint16_t)(((uint16_t)bytes[0] << 8u) | bytes[1]);
}

static uint32_t flac_read_u24_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 16u) |
           ((uint32_t)bytes[1] << 8u) | bytes[2];
}

static uint64_t flac_read_u64_be(const uint8_t *bytes)
{
    uint64_t value = 0u;
    size_t i;
    for (i = 0u; i < 8u; ++i) value = (value << 8u) | bytes[i];
    return value;
}

static uint8_t flac_crc8_update(uint8_t crc, uint8_t byte)
{
    unsigned bit;
    crc ^= byte;
    for (bit = 0u; bit < 8u; ++bit) {
        uint32_t shifted = (uint32_t)crc << 1u;
        if (crc & 0x80u) shifted ^= 0x07u;
        crc = (uint8_t)shifted;
    }
    return crc;
}

static uint16_t flac_crc16_update(uint16_t crc, uint8_t byte)
{
    unsigned bit;
    crc ^= (uint16_t)((uint16_t)byte << 8u);
    for (bit = 0u; bit < 8u; ++bit) {
        crc = (uint16_t)((crc & 0x8000u)
            ? (uint16_t)((crc << 1u) ^ 0x8005u)
            : (uint16_t)(crc << 1u));
    }
    return crc;
}

static uint16_t flac_crc16(const uint8_t *bytes, size_t size)
{
    uint16_t crc = 0u;
    size_t i;
    for (i = 0u; i < size; ++i) crc = flac_crc16_update(crc, bytes[i]);
    return crc;
}

static int flac_coded_number_size(const uint8_t *bytes,
                                 size_t size,
                                 uint8_t blocking_strategy,
                                 size_t *coded_size)
{
    uint8_t first;
    size_t length;
    uint64_t value;
    uint64_t minimum;
    size_t i;
    if (!bytes || !coded_size || !size) return QW_STATUS_ERR_FORMAT;
    first = bytes[0];
    if (first < 0x80u) {
        length = 1u;
        value = first;
        minimum = 0u;
    } else if (first >= 0xc2u && first <= 0xdfu) {
        length = 2u;
        value = first & 0x1fu;
        minimum = UINT64_C(0x80);
    } else if (first >= 0xe0u && first <= 0xefu) {
        length = 3u;
        value = first & 0x0fu;
        minimum = UINT64_C(0x800);
    } else if (first >= 0xf0u && first <= 0xf7u) {
        length = 4u;
        value = first & 0x07u;
        minimum = UINT64_C(0x10000);
    } else if (first >= 0xf8u && first <= 0xfbu) {
        length = 5u;
        value = first & 0x03u;
        minimum = UINT64_C(0x200000);
    } else if (first >= 0xfcu && first <= 0xfdu) {
        length = 6u;
        value = first & 0x01u;
        minimum = UINT64_C(0x4000000);
    } else if (first == 0xfeu) {
        length = 7u;
        value = 0u;
        minimum = UINT64_C(0x80000000);
    } else {
        return QW_STATUS_ERR_FORMAT;
    }
    if (length > size || (!blocking_strategy && length > 6u))
        return QW_STATUS_ERR_FORMAT;
    for (i = 1u; i < length; ++i) {
        if ((bytes[i] & 0xc0u) != 0x80u) return QW_STATUS_ERR_FORMAT;
        value = (value << 6u) | (bytes[i] & 0x3fu);
    }
    if (value < minimum ||
        (blocking_strategy && value > UINT64_C(0xfffffffff)) ||
        (!blocking_strategy && value > UINT64_C(0x7fffffff))) {
        return QW_STATUS_ERR_FORMAT;
    }
    *coded_size = length;
    return QW_STATUS_OK;
}

int qw_audio_flac_parse_streaminfo(const uint8_t *bytes,
                                   size_t size,
                                   QwAudioFlacSource *source)
{
    uint64_t packed;
    uint32_t min_block_size;
    uint32_t max_block_size;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size != QW_AUDIO_FLAC_STREAMINFO_SIZE) return QW_STATUS_ERR_FORMAT;
    min_block_size = flac_read_u16_be(bytes);
    max_block_size = flac_read_u16_be(bytes + 2u);
    if (min_block_size < 16u || max_block_size < min_block_size ||
        max_block_size > 65535u) {
        return QW_STATUS_ERR_FORMAT;
    }
    packed = flac_read_u64_be(bytes + 10u);
    source->sample_rate = (uint32_t)(packed >> 44u);
    source->channels = (uint16_t)(((packed >> 41u) & 0x07u) + 1u);
    source->bits_per_sample = (uint16_t)(((packed >> 36u) & 0x1fu) + 1u);
    source->stream_sample_count = packed & UINT64_C(0xfffffffff);
    if (!source->sample_rate || source->channels > 8u ||
        source->bits_per_sample < 4u || source->bits_per_sample > 32u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->min_block_size = min_block_size;
    source->max_block_size = max_block_size;
    memcpy(source->streaminfo, bytes, QW_AUDIO_FLAC_STREAMINFO_SIZE);
    return QW_STATUS_OK;
}

static int flac_parse_metadata(const uint8_t *bytes,
                               size_t size,
                               QwAudioFlacSource *source,
                               size_t *frame_offset)
{
    size_t position = FLAC_MARKER_SIZE;
    int last_block = 0;
    int streaminfo_seen = 0;
    if (size < FLAC_MARKER_SIZE ||
        memcmp(bytes, flac_marker, sizeof(flac_marker)) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    while (!last_block) {
        uint8_t header;
        uint8_t type;
        uint32_t block_size;
        size_t payload_offset;
        int rc;
        if (position > size || size - position < FLAC_METADATA_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        header = bytes[position];
        last_block = (header & 0x80u) != 0u;
        type = (uint8_t)(header & 0x7fu);
        block_size = flac_read_u24_be(bytes + position + 1u);
        payload_offset = position + FLAC_METADATA_HEADER_SIZE;
        if ((size_t)block_size > size - payload_offset)
            return QW_STATUS_ERR_FORMAT;
        if (!streaminfo_seen) {
            if (type != 0u || block_size != QW_AUDIO_FLAC_STREAMINFO_SIZE)
                return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_flac_parse_streaminfo(
                bytes + payload_offset, block_size, source);
            if (rc != QW_STATUS_OK) return rc;
            streaminfo_seen = 1;
        } else if (type == 0u) {
            return QW_STATUS_ERR_FORMAT;
        } else if (type == 127u) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        position = payload_offset + (size_t)block_size;
    }
    if (!streaminfo_seen || position >= size) return QW_STATUS_ERR_FORMAT;
    *frame_offset = position;
    return QW_STATUS_OK;
}

static int flac_parse_frame_header(const QwAudioFlacSource *source,
                                   size_t position,
                                   QwAudioFlacFrameHeader *header)
{
    const uint8_t *bytes;
    uint8_t block_code;
    uint8_t rate_code;
    uint8_t channel_assignment;
    uint8_t sample_size_code;
    uint8_t blocking_strategy;
    size_t offset;
    size_t coded_size;
    uint8_t crc = 0u;
    size_t i;
    if (!source || !header || !source->data ||
        position > source->frame_data_end ||
        source->frame_data_end - position < 5u) {
        return QW_STATUS_ERR_FORMAT;
    }
    bytes = source->data + position;
    if (bytes[0] != 0xffu || (bytes[1] & 0xfcu) != 0xf8u ||
        (bytes[1] & 0x02u) != 0u || (bytes[3] & 0x01u) != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    blocking_strategy = (uint8_t)(bytes[1] & 0x01u);
    block_code = (uint8_t)(bytes[2] >> 4u);
    rate_code = (uint8_t)(bytes[2] & 0x0fu);
    channel_assignment = (uint8_t)(bytes[3] >> 4u);
    sample_size_code = (uint8_t)((bytes[3] >> 1u) & 0x07u);

    switch (block_code) {
    case 0u: return QW_STATUS_ERR_UNSUPPORTED;
    case 1u: header->block_size = 192u; break;
    case 2u: case 3u: case 4u: case 5u:
        header->block_size = 576u << (block_code - 2u);
        break;
    case 6u:
    case 7u:
        header->block_size = 0u;
        break;
    case 8u: case 9u: case 10u: case 11u:
    case 12u: case 13u: case 14u: case 15u:
        header->block_size = 256u << (block_code - 8u);
        break;
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    switch (channel_assignment) {
    case 0u: case 1u: case 2u: case 3u:
    case 4u: case 5u: case 6u: case 7u:
        header->channels = (uint16_t)(channel_assignment + 1u);
        break;
    case 8u: case 9u: case 10u:
        header->channels = 2u;
        break;
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    switch (sample_size_code) {
    case 0u: header->bits_per_sample = source->bits_per_sample; break;
    case 1u: header->bits_per_sample = 8u; break;
    case 2u: header->bits_per_sample = 12u; break;
    case 3u: return QW_STATUS_ERR_UNSUPPORTED;
    case 4u: header->bits_per_sample = 16u; break;
    case 5u: header->bits_per_sample = 20u; break;
    case 6u: header->bits_per_sample = 24u; break;
    case 7u: header->bits_per_sample = 32u; break;
    default: return QW_STATUS_ERR_UNSUPPORTED;
    }
    offset = 4u;
    if (flac_coded_number_size(bytes + offset,
                               source->frame_data_end - position - offset,
                               blocking_strategy, &coded_size) != QW_STATUS_OK) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    offset += coded_size;
    if (block_code == 6u) {
        if (source->frame_data_end - position - offset < 1u)
            return QW_STATUS_ERR_FORMAT;
        header->block_size = (uint32_t)bytes[offset++] + 1u;
    } else if (block_code == 7u) {
        if (source->frame_data_end - position - offset < 2u)
            return QW_STATUS_ERR_FORMAT;
        header->block_size = (uint32_t)flac_read_u16_be(bytes + offset) + 1u;
        offset += 2u;
    }
    if (header->block_size > 65535u) return QW_STATUS_ERR_UNSUPPORTED;

    switch (rate_code) {
    case 0u: header->sample_rate = source->sample_rate; break;
    case 1u: header->sample_rate = 88200u; break;
    case 2u: header->sample_rate = 176400u; break;
    case 3u: header->sample_rate = 192000u; break;
    case 4u: header->sample_rate = 8000u; break;
    case 5u: header->sample_rate = 16000u; break;
    case 6u: header->sample_rate = 22050u; break;
    case 7u: header->sample_rate = 24000u; break;
    case 8u: header->sample_rate = 32000u; break;
    case 9u: header->sample_rate = 44100u; break;
    case 10u: header->sample_rate = 48000u; break;
    case 11u: header->sample_rate = 96000u; break;
    case 12u:
        if (source->frame_data_end - position - offset < 1u)
            return QW_STATUS_ERR_FORMAT;
        header->sample_rate = (uint32_t)bytes[offset++] * 1000u;
        break;
    case 13u:
        if (source->frame_data_end - position - offset < 2u)
            return QW_STATUS_ERR_FORMAT;
        header->sample_rate = flac_read_u16_be(bytes + offset);
        offset += 2u;
        break;
    case 14u:
        if (source->frame_data_end - position - offset < 2u)
            return QW_STATUS_ERR_FORMAT;
        header->sample_rate =
            (uint32_t)flac_read_u16_be(bytes + offset) * 10u;
        offset += 2u;
        break;
    case 15u:
        return QW_STATUS_ERR_UNSUPPORTED;
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!header->sample_rate || header->channels != source->channels ||
        header->sample_rate != source->sample_rate ||
        header->bits_per_sample != source->bits_per_sample) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->max_block_size &&
        header->block_size > source->max_block_size) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->frame_data_end - position - offset < 1u)
        return QW_STATUS_ERR_FORMAT;
    for (i = 0u; i < offset; ++i) crc = flac_crc8_update(crc, bytes[i]);
    if (crc != bytes[offset]) return QW_STATUS_ERR_UNSUPPORTED;
    header->header_size = offset + 1u;
    header->blocking_strategy = blocking_strategy;
    return QW_STATUS_OK;
}

int qw_audio_flac_probe(const uint8_t *bytes, size_t size)
{
    return bytes && size >= sizeof(flac_marker) &&
           memcmp(bytes, flac_marker, sizeof(flac_marker)) == 0;
}

int qw_audio_flac_next_frame(const QwAudioFlacSource *source,
                             size_t position,
                             QwAudioFlacFrame *frame)
{
    QwAudioFlacFrameHeader header = {0};
    size_t frame_end;
    size_t candidate;
    size_t crc_position;
    uint16_t crc = 0u;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position < source->first_frame_offset ||
        position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;
    rc = flac_parse_frame_header(source, position, &header);
    if (rc != QW_STATUS_OK) return rc;
    if (header.blocking_strategy != source->blocking_strategy)
        return QW_STATUS_ERR_UNSUPPORTED;

    frame_end = source->frame_data_end;
    candidate = position + header.header_size;
    crc_position = position;
    for (; candidate <= source->frame_data_end - 2u; ++candidate) {
        size_t crc_end = candidate - 2u;
        while (crc_position < crc_end) {
            crc = flac_crc16_update(crc, source->data[crc_position++]);
        }
        if (candidate + 1u >= source->frame_data_end ||
            source->data[candidate] != 0xffu ||
            (source->data[candidate + 1u] & 0xfcu) != 0xf8u) {
            continue;
        }
        {
            QwAudioFlacFrameHeader next_header = {0};
            if (flac_parse_frame_header(source, candidate, &next_header) == QW_STATUS_OK &&
                next_header.blocking_strategy == source->blocking_strategy &&
                crc == flac_read_u16_be(source->data + candidate - 2u)) {
                frame_end = candidate;
                break;
            }
        }
    }
    if (frame_end <= position + header.header_size + 2u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (flac_crc16(source->data + position,
                   frame_end - position - 2u) !=
        flac_read_u16_be(source->data + frame_end - 2u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    frame->data = source->data + position;
    frame->size = frame_end - position;
    frame->next_position = frame_end;
    frame->sample_count = header.block_size;
    return 1;
}

int qw_audio_flac_parse(const uint8_t *bytes,
                        size_t size,
                        QwAudioFlacSource *source,
                        QwConvertReport *report)
{
    QwAudioFlacFrameHeader first_header = {0};
    size_t position;
    uint64_t sample_count = 0u;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (!qw_audio_flac_probe(bytes, size)) return QW_STATUS_ERR_UNSUPPORTED;
    source->data = bytes;
    source->size = size;
    rc = flac_parse_metadata(bytes, size, source,
                             &source->first_frame_offset);
    if (rc != QW_STATUS_OK) return rc;
    source->frame_data_end = size;
    rc = flac_parse_frame_header(source, source->first_frame_offset,
                                 &first_header);
    if (rc != QW_STATUS_OK) return rc;
    source->blocking_strategy = first_header.blocking_strategy;
    position = source->first_frame_offset;
    while (position < source->frame_data_end) {
        QwAudioFlacFrame frame;
        rc = qw_audio_flac_next_frame(source, position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (frame.sample_count > source->max_block_size ||
            (frame.next_position < source->frame_data_end &&
             frame.sample_count < source->min_block_size) ||
            sample_count > UINT64_MAX - frame.sample_count) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        sample_count += frame.sample_count;
        position = frame.next_position;
    }
    if (!sample_count || position != source->frame_data_end ||
        (source->stream_sample_count &&
         source->stream_sample_count != sample_count)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    source->sample_count = sample_count;
    return QW_STATUS_OK;
}
