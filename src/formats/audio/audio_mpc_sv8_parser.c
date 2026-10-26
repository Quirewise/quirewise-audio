#include "audio_mpc_parser_internal.h"
#include "audio_mpc_sv8_parser_internal.h"

#include "audio_trailing_tags.h"
#include "quirewise/status.h"

#include <string.h>

typedef struct MpcChunk {
    uint8_t key[2];
    size_t payload_position;
    size_t payload_size;
    size_t next_position;
} MpcChunk;

static int mpc_read_varint(const uint8_t *data,
                           size_t end,
                           size_t *position,
                           uint64_t *value,
                           size_t *length)
{
    uint64_t result = 0u;
    size_t count = 0u;
    uint8_t byte;

    if (!data || !position || !value || *position > end)
        return QW_STATUS_ERR_FORMAT;
    do {
        uint64_t group;
        if (*position >= end || count >= 10u) return QW_STATUS_ERR_FORMAT;
        byte = data[(*position)++];
        group = byte & 0x7fu;
        if (result > (UINT64_MAX - group) / 128u)
            return QW_STATUS_ERR_FORMAT;
        result = result * 128u + group;
        ++count;
    } while (byte & 0x80u);

    *value = result;
    if (length) *length = count;
    return QW_STATUS_OK;
}

static int mpc_read_chunk(const uint8_t *data,
                          size_t end,
                          size_t position,
                          MpcChunk *chunk)
{
    size_t cursor;
    size_t header_size;
    uint64_t packet_size;
    size_t index;

    if (!data || !chunk || position > end || end - position < 3u)
        return QW_STATUS_ERR_FORMAT;
    chunk->key[0] = data[position];
    chunk->key[1] = data[position + 1u];
    for (index = 0u; index < 2u; ++index) {
        if (chunk->key[index] < 'A' || chunk->key[index] > 'Z')
            return QW_STATUS_ERR_FORMAT;
    }
    cursor = position + 2u;
    if (mpc_read_varint(data, end, &cursor, &packet_size, NULL) != QW_STATUS_OK)
        return QW_STATUS_ERR_FORMAT;
    header_size = cursor - position;
    if (packet_size < (uint64_t)header_size ||
        packet_size > (uint64_t)(end - position) ||
        packet_size > (uint64_t)SIZE_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    chunk->payload_position = cursor;
    chunk->payload_size = (size_t)packet_size - header_size;
    chunk->next_position = position + (size_t)packet_size;
    return QW_STATUS_OK;
}

static int mpc_parse_sv8_stream_header(const uint8_t *data,
                                       const MpcChunk *chunk,
                                       QwAudioMpcSource *source)
{
    static const uint32_t sample_rates[4] = {
        44100u, 48000u, 37800u, 32000u
    };
    size_t position;
    size_t end;
    uint64_t sample_count;
    uint64_t beginning_silence;
    unsigned sample_rate_index;
    unsigned channels;
    unsigned max_bands;
    unsigned frame_exponent;
    int rc;

    if (chunk->payload_size < 9u) return QW_STATUS_ERR_FORMAT;
    if (!qw_audio_mpc_read_u32_le(data + chunk->payload_position))
        return QW_STATUS_ERR_FORMAT;
    position = chunk->payload_position + 4u;
    end = chunk->payload_position + chunk->payload_size;
    if (data[position++] != 8u) return QW_STATUS_ERR_UNSUPPORTED;
    rc = mpc_read_varint(data, end, &position, &sample_count, NULL);
    if (rc != QW_STATUS_OK) return rc;
    rc = mpc_read_varint(data, end, &position, &beginning_silence, NULL);
    if (rc != QW_STATUS_OK || end - position < 2u)
        return QW_STATUS_ERR_FORMAT;

    source->decoder_extradata[0] = data[position++];
    source->decoder_extradata[1] = data[position];
    source->decoder_extradata_size = 2u;
    sample_rate_index = source->decoder_extradata[0] >> 5u;
    max_bands = source->decoder_extradata[0] & 0x1fu;
    channels = (source->decoder_extradata[1] >> 4u) + 1u;
    frame_exponent = (source->decoder_extradata[1] & 0x03u) * 2u;

    if (sample_rate_index >= sizeof(sample_rates) / sizeof(sample_rates[0]))
        return QW_STATUS_ERR_UNSUPPORTED;
    if (max_bands > 30u) return QW_STATUS_ERR_FORMAT;
    source->sample_rate = sample_rates[sample_rate_index];
    source->channels = (uint16_t)channels;
    source->sample_count = sample_count;
    source->beginning_silence_samples = beginning_silence;
    source->frame_samples = UINT32_C(1152) << frame_exponent;
    return QW_STATUS_OK;
}

int qw_audio_mpc_sv8_parse(const uint8_t *data,
                           size_t size,
                           QwAudioMpcSource *source)
{
    size_t position = 4u;
    size_t header_end = 0u;
    size_t audio_end;
    int have_stream_header = 0;
    int have_stream_end = 0;
    uint64_t packet_count = 0u;
    MpcChunk chunk;
    int rc;

    if (!data || !size || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || memcmp(data, "MPCK", 4u) != 0)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_trailing_tags_audio_end(data, size, &audio_end);
    if (rc != QW_STATUS_OK) return rc;
    if (audio_end < 4u) return QW_STATUS_ERR_FORMAT;
    source->data = data;
    source->size = size;
    source->is_sv8 = 1u;

    while (position < audio_end) {
        rc = mpc_read_chunk(data, audio_end, position, &chunk);
        if (rc != QW_STATUS_OK) return rc;
        if (chunk.key[0] == 'S' && chunk.key[1] == 'H') {
            if (have_stream_header || packet_count || have_stream_end)
                return QW_STATUS_ERR_FORMAT;
            rc = mpc_parse_sv8_stream_header(data, &chunk, source);
            if (rc != QW_STATUS_OK) return rc;
            have_stream_header = 1;
            header_end = chunk.next_position;
        } else if (chunk.key[0] == 'A' && chunk.key[1] == 'P') {
            if (!have_stream_header || have_stream_end || !chunk.payload_size)
                return QW_STATUS_ERR_FORMAT;
            if (source->audio_packet_bytes >
                UINT64_MAX - (uint64_t)chunk.payload_size) {
                return QW_STATUS_ERR_FORMAT;
            }
            source->audio_packet_bytes += (uint64_t)chunk.payload_size;
            ++packet_count;
        } else if (chunk.key[0] == 'S' && chunk.key[1] == 'E') {
            if (!have_stream_header || have_stream_end ||
                chunk.payload_size != 0u || !packet_count) {
                return QW_STATUS_ERR_FORMAT;
            }
            source->stream_end_position = position;
            have_stream_end = 1;
            break;
        }
        if (chunk.next_position <= position) return QW_STATUS_ERR_FORMAT;
        position = chunk.next_position;
    }

    if (!have_stream_header) return QW_STATUS_ERR_UNSUPPORTED;
    if (!packet_count) return QW_STATUS_ERR_FORMAT;
    if (!have_stream_end) source->stream_end_position = audio_end;
    source->packet_position = header_end;
    return QW_STATUS_OK;
}

int qw_audio_mpc_sv8_next_packet(QwAudioMpcSource *source,
                                 QwAudioMpcPacket *packet)
{
    MpcChunk chunk;
    int rc;

    while (source->packet_position < source->stream_end_position) {
        size_t position = source->packet_position;
        rc = mpc_read_chunk(source->data, source->stream_end_position,
                            position, &chunk);
        if (rc != QW_STATUS_OK) return rc;
        source->packet_position = chunk.next_position;
        if (chunk.key[0] == 'A' && chunk.key[1] == 'P') {
            if (!chunk.payload_size) return QW_STATUS_ERR_FORMAT;
            packet->data = source->data + chunk.payload_position;
            packet->size = chunk.payload_size;
            return 1;
        }
        if (chunk.key[0] == 'S' && chunk.key[1] == 'H')
            return QW_STATUS_ERR_FORMAT;
    }
    return 0;
}
