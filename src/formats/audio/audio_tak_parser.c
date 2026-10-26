#include "audio_tak_parser.h"

#include "audio_trailing_tags.h"
#include "quirewise/status.h"

#include <string.h>

enum {
    TAK_METADATA_HEADER_SIZE = 4u,
    TAK_METADATA_CRC_SIZE = 3u,
    TAK_FRAME_HEADER_MAX_SIZE = 40u,
    TAK_FORMAT_CHANNEL_COUNT_MAX = 16u,
    TAK_DECODER_CHANNEL_COUNT_MAX = 6u,
    TAK_FRAME_FLAG_IS_LAST = 1u,
    TAK_FRAME_FLAG_HAS_INFO = 2u,
    TAK_FRAME_FLAG_HAS_METADATA = 4u,
    TAK_METADATA_END = 0u,
    TAK_METADATA_STREAMINFO = 1u,
    TAK_METADATA_ENCODER = 4u,
    TAK_METADATA_MD5 = 6u,
    TAK_METADATA_LAST_FRAME = 7u,
    TAK_SAMPLE_RATE_MIN = 6000u,
    TAK_BITS_PER_SAMPLE_MIN = 8u,
    TAK_CHANNELS_MIN = 1u,
    TAK_CRC24_POLYNOMIAL = 0x864cfbu
};

typedef struct QwAudioTakBitReader {
    const uint8_t *data;
    size_t bit_position;
    size_t bit_limit;
} QwAudioTakBitReader;

typedef struct QwAudioTakStreamInfo {
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t frame_samples;
    uint32_t channel_mask;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t codec_type;
} QwAudioTakStreamInfo;

typedef struct QwAudioTakFrameHeader {
    size_t size;
    uint32_t sample_count;
    uint32_t frame_number;
    int is_last;
} QwAudioTakFrameHeader;

static uint32_t tak_read_u24_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u);
}

static uint32_t tak_read_u24_be(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 16u) | ((uint32_t)bytes[1] << 8u) |
           (uint32_t)bytes[2];
}

static uint32_t tak_read_u32_le(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static uint64_t tak_read_u40_le(const uint8_t *bytes)
{
    return (uint64_t)bytes[0] | ((uint64_t)bytes[1] << 8u) |
           ((uint64_t)bytes[2] << 16u) | ((uint64_t)bytes[3] << 24u) |
           ((uint64_t)bytes[4] << 32u);
}

static uint32_t tak_crc24(const uint8_t *bytes, size_t size)
{
    uint32_t crc = UINT32_C(0xce04b7);
    size_t i;
    for (i = 0u; i < size; ++i) {
        unsigned bit;
        crc ^= (uint32_t)bytes[i] << 16u;
        for (bit = 0u; bit < 8u; ++bit) {
            crc = (crc & UINT32_C(0x800000))
                ? ((crc << 1u) ^ TAK_CRC24_POLYNOMIAL)
                : (crc << 1u);
            crc &= UINT32_C(0xffffff);
        }
    }
    return crc;
}

static int tak_crc24_matches(const uint8_t *bytes, size_t size)
{
    if (!bytes || size < TAK_METADATA_CRC_SIZE) return 0;
    return tak_crc24(bytes, size - TAK_METADATA_CRC_SIZE) ==
           tak_read_u24_be(bytes + size - TAK_METADATA_CRC_SIZE);
}

static int tak_read_bits(QwAudioTakBitReader *reader,
                         unsigned bit_count,
                         uint64_t *value)
{
    uint64_t result = 0u;
    unsigned bit;
    if (!reader || !value || bit_count > 64u ||
        reader->bit_position > reader->bit_limit ||
        bit_count > reader->bit_limit - reader->bit_position) {
        return 0;
    }
    for (bit = 0u; bit < bit_count; ++bit) {
        size_t position = reader->bit_position++;
        uint64_t next = (reader->data[position / 8u] >> (position % 8u)) & 1u;
        result |= next << bit;
    }
    *value = result;
    return 1;
}

static int tak_skip_bits(QwAudioTakBitReader *reader, unsigned bit_count)
{
    uint64_t ignored;
    return tak_read_bits(reader, bit_count, &ignored);
}

static uint32_t tak_frame_samples(uint32_t sample_rate, uint32_t frame_type)
{
    static const uint32_t fixed_samples[] = {
        0u, 0u, 0u, 0u, 4096u, 8192u, 16384u, 512u, 1024u, 2048u
    };
    static const uint8_t duration_quants[] = {3u, 4u, 6u, 8u};
    uint64_t samples;
    if (frame_type < sizeof(duration_quants)) {
        samples = ((uint64_t)sample_rate * duration_quants[frame_type]) >> 5u;
        if (!samples || samples > 16384u) return 0u;
        return (uint32_t)samples;
    }
    if (frame_type >= sizeof(fixed_samples) / sizeof(fixed_samples[0]))
        return 0u;
    return fixed_samples[frame_type];
}

static uint32_t tak_channel_layout_bit(uint32_t channel_id)
{
    static const uint32_t layout_bits[] = {
        0u,
        UINT32_C(1) << 0u,  UINT32_C(1) << 1u,
        UINT32_C(1) << 2u,  UINT32_C(1) << 3u,
        UINT32_C(1) << 4u,  UINT32_C(1) << 5u,
        UINT32_C(1) << 6u,  UINT32_C(1) << 7u,
        UINT32_C(1) << 8u,  UINT32_C(1) << 9u,
        UINT32_C(1) << 10u, UINT32_C(1) << 11u,
        UINT32_C(1) << 12u, UINT32_C(1) << 13u,
        UINT32_C(1) << 14u, UINT32_C(1) << 15u,
        UINT32_C(1) << 16u, UINT32_C(1) << 17u
    };
    if (channel_id >= sizeof(layout_bits) / sizeof(layout_bits[0])) return 0u;
    return layout_bits[channel_id];
}

static unsigned tak_popcount32(uint32_t value)
{
    unsigned count = 0u;
    while (value) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

static int tak_parse_streaminfo_bits(QwAudioTakBitReader *reader,
                                     QwAudioTakStreamInfo *info)
{
    uint64_t value;
    uint64_t layout_present;
    uint32_t frame_type;
    uint32_t sample_rate;
    uint32_t channels;
    uint32_t data_type;
    uint32_t channel_mask = 0u;
    uint32_t codec_type;
    int layout_complete = 1;
    uint32_t channel;

    if (!reader || !info) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(info, 0, sizeof(*info));

    if (!tak_read_bits(reader, 6u, &value)) return QW_STATUS_ERR_FORMAT;
    codec_type = (uint32_t)value;
    if (codec_type != 2u && codec_type != 4u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (!tak_skip_bits(reader, 4u) ||
        !tak_read_bits(reader, 4u, &value)) {
        return QW_STATUS_ERR_FORMAT;
    }
    frame_type = (uint32_t)value;
    if (!tak_read_bits(reader, 35u, &info->sample_count) ||
        !tak_read_bits(reader, 3u, &value)) {
        return QW_STATUS_ERR_FORMAT;
    }
    data_type = (uint32_t)value;
    if (data_type != 0u) return QW_STATUS_ERR_UNSUPPORTED;
    if (!tak_read_bits(reader, 18u, &value)) return QW_STATUS_ERR_FORMAT;
    sample_rate = (uint32_t)value + TAK_SAMPLE_RATE_MIN;
    if (!tak_read_bits(reader, 5u, &value)) return QW_STATUS_ERR_FORMAT;
    info->bits_per_sample = (uint16_t)(value + TAK_BITS_PER_SAMPLE_MIN);
    if (!tak_read_bits(reader, 4u, &value)) return QW_STATUS_ERR_FORMAT;
    channels = (uint32_t)value + TAK_CHANNELS_MIN;
    if (sample_rate < TAK_SAMPLE_RATE_MIN || !sample_rate ||
        (info->bits_per_sample != 8u && info->bits_per_sample != 16u &&
         info->bits_per_sample != 24u) ||
        channels > TAK_FORMAT_CHANNEL_COUNT_MAX ||
        (codec_type == 2u && channels > 2u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (channels > TAK_DECODER_CHANNEL_COUNT_MAX)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (!tak_read_bits(reader, 1u, &value)) return QW_STATUS_ERR_FORMAT;
    if (value) {
        if (!tak_skip_bits(reader, 5u) ||
            !tak_read_bits(reader, 1u, &layout_present)) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (layout_present) {
            for (channel = 0u; channel < channels; ++channel) {
                uint32_t bit;
                if (!tak_read_bits(reader, 6u, &value))
                    return QW_STATUS_ERR_FORMAT;
                bit = tak_channel_layout_bit((uint32_t)value);
                if (!bit || (channel_mask & bit)) layout_complete = 0;
                channel_mask |= bit;
            }
        }
    }
    info->frame_samples = tak_frame_samples(sample_rate, frame_type);
    if (!info->frame_samples) return QW_STATUS_ERR_UNSUPPORTED;
    if (layout_complete && tak_popcount32(channel_mask) == channels)
        info->channel_mask = channel_mask;
    info->sample_rate = sample_rate;
    info->channels = (uint16_t)channels;
    info->codec_type = (uint8_t)codec_type;
    return QW_STATUS_OK;
}

static int tak_parse_streaminfo(const uint8_t *bytes,
                                size_t size,
                                QwAudioTakStreamInfo *info)
{
    QwAudioTakBitReader reader;
    if (!bytes || !info || !size || size > TAK_FRAME_HEADER_MAX_SIZE)
        return QW_STATUS_ERR_FORMAT;
    reader.data = bytes;
    reader.bit_position = 0u;
    reader.bit_limit = size * 8u;
    return tak_parse_streaminfo_bits(&reader, info);
}

static int tak_parse_frame_header(const uint8_t *bytes,
                                 size_t size,
                                 const QwAudioTakSource *source,
                                 QwAudioTakFrameHeader *header)
{
    QwAudioTakBitReader reader;
    QwAudioTakStreamInfo inline_info;
    uint64_t value;
    uint32_t flags;
    size_t header_size;
    size_t limit;
    int rc;

    if (!bytes || !source || !header) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(header, 0, sizeof(*header));
    if (size < 8u) return QW_STATUS_ERR_FORMAT;
    limit = size < TAK_FRAME_HEADER_MAX_SIZE ? size : TAK_FRAME_HEADER_MAX_SIZE;
    reader.data = bytes;
    reader.bit_position = 0u;
    reader.bit_limit = limit * 8u;
    if (!tak_read_bits(&reader, 16u, &value) || value != UINT64_C(0xa0ff) ||
        !tak_read_bits(&reader, 3u, &value)) {
        return QW_STATUS_ERR_FORMAT;
    }
    flags = (uint32_t)value;
    if (!tak_read_bits(&reader, 21u, &value)) return QW_STATUS_ERR_FORMAT;
    header->frame_number = (uint32_t)value;
    header->is_last = (flags & TAK_FRAME_FLAG_IS_LAST) != 0u;
    if (header->is_last) {
        if (!tak_read_bits(&reader, 14u, &value) ||
            !tak_skip_bits(&reader, 2u)) {
            return QW_STATUS_ERR_FORMAT;
        }
        header->sample_count = (uint32_t)value + 1u;
    }
    if (flags & TAK_FRAME_FLAG_HAS_INFO) {
        uint64_t extra_header;
        rc = tak_parse_streaminfo_bits(&reader, &inline_info);
        if (rc != QW_STATUS_OK) return rc;
        if (!tak_read_bits(&reader, 6u, &extra_header))
            return QW_STATUS_ERR_FORMAT;
        if (extra_header && !tak_skip_bits(&reader, 25u))
            return QW_STATUS_ERR_FORMAT;
        reader.bit_position = (reader.bit_position + 7u) & ~(size_t)7u;
        if (inline_info.codec_type != source->codec_type ||
            inline_info.sample_rate != source->sample_rate ||
            inline_info.channels != source->channels ||
            inline_info.bits_per_sample != source->bits_per_sample ||
            inline_info.frame_samples != source->frame_samples) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (source->channel_mask && inline_info.channel_mask &&
            inline_info.channel_mask != source->channel_mask) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
    }
    if (flags & TAK_FRAME_FLAG_HAS_METADATA) return QW_STATUS_ERR_UNSUPPORTED;
    if (!tak_skip_bits(&reader, 24u)) return QW_STATUS_ERR_FORMAT;
    if (reader.bit_position % 8u) return QW_STATUS_ERR_FORMAT;
    header_size = reader.bit_position / 8u;
    if (header_size < 8u || header_size > limit ||
        !tak_crc24_matches(bytes, header_size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!header->is_last) header->sample_count = source->frame_samples;
    if (!header->sample_count || header->sample_count > source->frame_samples)
        return QW_STATUS_ERR_FORMAT;
    header->size = header_size;
    return QW_STATUS_OK;
}

static int tak_peek_frame_number(const uint8_t *bytes,
                                 size_t size,
                                 uint32_t *frame_number)
{
    QwAudioTakBitReader reader;
    uint64_t value;
    size_t limit;
    if (!bytes || !frame_number || size < 5u) return QW_STATUS_ERR_FORMAT;
    limit = size < TAK_FRAME_HEADER_MAX_SIZE ? size : TAK_FRAME_HEADER_MAX_SIZE;
    reader.data = bytes;
    reader.bit_position = 0u;
    reader.bit_limit = limit * 8u;
    if (!tak_read_bits(&reader, 16u, &value) || value != UINT64_C(0xa0ff) ||
        !tak_skip_bits(&reader, 3u) ||
        !tak_read_bits(&reader, 21u, &value)) {
        return QW_STATUS_ERR_FORMAT;
    }
    *frame_number = (uint32_t)value;
    return QW_STATUS_OK;
}

static int tak_validate_frame_stream(const QwAudioTakSource *source);

int qw_audio_tak_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioTakSource *source)
{
    size_t position;
    size_t payload_limit;
    size_t streaminfo_size = 0u;
    const uint8_t *streaminfo = NULL;
    uint64_t last_frame_end = 0u;
    int has_last_frame = 0;
    int rc;

    if (!bytes || !size || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || memcmp(bytes, "tBaK", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_trailing_tags_audio_end(bytes, size, &payload_limit);
    if (rc != QW_STATUS_OK) return rc;
    position = 4u;
    while (position <= payload_limit &&
           payload_limit - position >= TAK_METADATA_HEADER_SIZE) {
        uint8_t raw_type = bytes[position];
        uint8_t type = raw_type & 0x7fu;
        uint32_t block_size = tak_read_u24_le(bytes + position + 1u);
        size_t payload_position = position + TAK_METADATA_HEADER_SIZE;
        size_t payload_size;
        const uint8_t *block;
        if (type == TAK_METADATA_END) {
            if (block_size != 0u) return QW_STATUS_ERR_FORMAT;
            position = payload_position;
            break;
        }
        if (block_size < TAK_METADATA_CRC_SIZE ||
            block_size > payload_limit - payload_position) {
            return QW_STATUS_ERR_FORMAT;
        }
        payload_size = (size_t)block_size - TAK_METADATA_CRC_SIZE;
        block = bytes + payload_position;
        if (!tak_crc24_matches(block, block_size)) return QW_STATUS_ERR_FORMAT;
        if (type == TAK_METADATA_STREAMINFO) {
            QwAudioTakStreamInfo info;
            if (streaminfo) return QW_STATUS_ERR_FORMAT;
            rc = tak_parse_streaminfo(block, payload_size, &info);
            if (rc != QW_STATUS_OK) return rc;
            streaminfo = block;
            streaminfo_size = payload_size;
            source->sample_count = info.sample_count;
            source->sample_rate = info.sample_rate;
            source->frame_samples = info.frame_samples;
            source->channel_mask = info.channel_mask;
            source->channels = info.channels;
            source->bits_per_sample = info.bits_per_sample;
            source->codec_type = info.codec_type;
        } else if (type == TAK_METADATA_LAST_FRAME) {
            uint64_t frame_offset;
            uint32_t frame_size;
            if (payload_size != 8u) return QW_STATUS_ERR_FORMAT;
            frame_offset = tak_read_u40_le(block);
            frame_size = tak_read_u24_le(block + 5u);
            if (!frame_size || frame_offset > UINT64_MAX - frame_size)
                return QW_STATUS_ERR_FORMAT;
            last_frame_end = frame_offset + frame_size;
            has_last_frame = 1;
        } else if (type == TAK_METADATA_MD5 && payload_size != 16u) {
            return QW_STATUS_ERR_FORMAT;
        } else if (type == TAK_METADATA_ENCODER && !payload_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        position = payload_position + block_size;
    }
    if (position > payload_limit || payload_limit - position < 8u || !streaminfo)
        return QW_STATUS_ERR_FORMAT;
    source->data = bytes;
    source->size = size;
    source->audio_data_offset = position;
    source->audio_data_end = payload_limit;
    source->streaminfo = streaminfo;
    source->streaminfo_size = streaminfo_size;
    source->has_metadata = 1;
    if (has_last_frame) {
        if (last_frame_end < source->audio_data_offset ||
            last_frame_end > payload_limit || last_frame_end > SIZE_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        source->audio_data_end = (size_t)last_frame_end;
    }
    if (source->audio_data_end <= source->audio_data_offset)
        return QW_STATUS_ERR_FORMAT;
    return tak_validate_frame_stream(source);
}

int qw_audio_tak_next_frame(const QwAudioTakSource *source,
                            size_t position,
                            QwAudioTakFrame *frame)
{
    QwAudioTakFrameHeader current;
    size_t frame_end;
    size_t candidate;
    uint32_t expected_frame_number;
    int rc;
    if (!source || !frame || !source->data)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->audio_data_end) return 0;
    if (position < source->audio_data_offset ||
        position > source->audio_data_end) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (source->audio_data_end - position < 8u) return QW_STATUS_ERR_FORMAT;
    rc = tak_parse_frame_header(source->data + position,
                                source->audio_data_end - position,
                                source, &current);
    if (rc != QW_STATUS_OK) return rc;
    frame_end = source->audio_data_end;
    if (!current.is_last) {
        expected_frame_number = (current.frame_number + 1u) & 0x1fffffu;
        candidate = position + current.size;
        while (candidate <= source->audio_data_end - 8u) {
            if (source->data[candidate] == 0xffu &&
                source->data[candidate + 1u] == 0xa0u) {
                QwAudioTakFrameHeader next;
                rc = tak_parse_frame_header(source->data + candidate,
                                            source->audio_data_end - candidate,
                                            source, &next);
                if (rc == QW_STATUS_OK &&
                    next.frame_number == expected_frame_number) {
                    frame_end = candidate;
                    break;
                }
                if (rc == QW_STATUS_ERR_UNSUPPORTED) {
                    uint32_t candidate_frame_number;
                    if (tak_peek_frame_number(source->data + candidate,
                                              source->audio_data_end - candidate,
                                              &candidate_frame_number) == QW_STATUS_OK &&
                        candidate_frame_number == expected_frame_number) {
                        return QW_STATUS_ERR_UNSUPPORTED;
                    }
                }
            }
            ++candidate;
        }
    }
    if (frame_end <= position + current.size) return QW_STATUS_ERR_FORMAT;
    frame->data = source->data + position;
    frame->size = frame_end - position;
    frame->next_position = frame_end;
    frame->sample_count = current.sample_count;
    frame->frame_number = current.frame_number;
    frame->is_last = current.is_last;
    return 1;
}

static int tak_validate_frame_stream(const QwAudioTakSource *source)
{
    size_t position;
    size_t frame_count = 0u;
    uint32_t previous_frame_number = 0u;
    uint64_t decoded_samples = 0u;
    int has_previous_frame = 0;

    if (!source || !source->data ||
        source->audio_data_offset >= source->audio_data_end) {
        return QW_STATUS_ERR_FORMAT;
    }
    position = source->audio_data_offset;
    while (position < source->audio_data_end) {
        QwAudioTakFrame frame;
        uint64_t duration = 0u;
        int rc = qw_audio_tak_next_frame(source, position, &frame);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (!frame.size || frame.next_position <= position ||
            frame.next_position > source->audio_data_end) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (has_previous_frame &&
            frame.frame_number != ((previous_frame_number + 1u) & 0x1fffffu)) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (source->sample_count) {
            uint64_t remaining;
            if (decoded_samples >= source->sample_count) return QW_STATUS_ERR_FORMAT;
            remaining = source->sample_count - decoded_samples;
            duration = frame.sample_count < remaining
                ? frame.sample_count : remaining;
        } else {
            duration = frame.sample_count;
        }
        if (!duration || duration > UINT64_MAX - decoded_samples)
            return QW_STATUS_ERR_FORMAT;
        decoded_samples += duration;
        position = frame.next_position;
        previous_frame_number = frame.frame_number;
        has_previous_frame = 1;
        if (frame_count == SIZE_MAX) return QW_STATUS_ERR_FORMAT;
        ++frame_count;
        if (frame.is_last && position != source->audio_data_end)
            return QW_STATUS_ERR_FORMAT;
    }
    if (!frame_count || position != source->audio_data_end ||
        (source->sample_count && decoded_samples != source->sample_count)) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}
