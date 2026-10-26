#include "audio_asf_internal.h"

#include <stdlib.h>
#include <string.h>

static int asf_parse_data_packet_header(QwAudioAsfSource *source)
{
    size_t cursor;
    size_t physical_end;
    size_t payload_end;
    uint32_t packet_length;
    uint32_t sequence_number;
    uint32_t padding_size;
    uint32_t effective_packet_length;
    uint8_t flags;
    uint8_t property;
    uint8_t payload_flags;
    unsigned payload_length_type = 0u;
    uint8_t payload_count = 1u;
    int rc;

    if (source->packet_position >= source->data_end) return 0;
    if (source->data_packet_count &&
        source->data_packets_read >= source->data_packet_count) {
        return 0;
    }
    if (source->packet_position > source->data_end ||
        source->data_end - source->packet_position < 2u) return QW_STATUS_ERR_FORMAT;
    cursor = source->packet_position;
    if (source->data_end - cursor >= 3u &&
        source->data[cursor] == 0x82u && source->data[cursor + 1u] == 0u &&
        source->data[cursor + 2u] == 0u) {
        cursor += 3u;
    } else if (source->data[cursor] & 0x80u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!asf_range_is_valid(cursor, 2u, source->data_end)) return QW_STATUS_ERR_FORMAT;
    flags = source->data[cursor++];
    property = source->data[cursor++];
    rc = asf_read_field(source->data, source->data_end, &cursor,
                        (flags >> 5u) & 3u, source->maximum_packet_size,
                        &packet_length);
    if (rc != QW_STATUS_OK) return rc;
    rc = asf_read_field(source->data, source->data_end, &cursor,
                        (flags >> 1u) & 3u, 0u, &sequence_number);
    if (rc != QW_STATUS_OK) return rc;
    (void)sequence_number;
    rc = asf_read_field(source->data, source->data_end, &cursor,
                        (flags >> 3u) & 3u, 0u, &padding_size);
    if (rc != QW_STATUS_OK) return rc;
    if (!packet_length) return QW_STATUS_ERR_FORMAT;
    if (packet_length < source->minimum_packet_size) {
        uint32_t missing = source->minimum_packet_size - packet_length;
        if (padding_size > UINT32_MAX - missing) return QW_STATUS_ERR_FORMAT;
        padding_size += missing;
        effective_packet_length = source->minimum_packet_size;
    } else {
        effective_packet_length = packet_length;
    }
    if (padding_size >= effective_packet_length ||
        effective_packet_length > source->data_end - source->packet_position)
        return QW_STATUS_ERR_FORMAT;
    physical_end = source->packet_position + effective_packet_length;
    payload_end = physical_end - padding_size;
    if (!asf_range_is_valid(cursor, 6u, payload_end)) return QW_STATUS_ERR_FORMAT;
    cursor += 6u;
    if (flags & 0x01u) {
        if (!asf_range_is_valid(cursor, 1u, payload_end)) return QW_STATUS_ERR_FORMAT;
        payload_flags = source->data[cursor++];
        payload_count = payload_flags & 0x3fu;
        payload_length_type = (unsigned)(payload_flags >> 6u);
        if (!payload_count || !payload_length_type) return QW_STATUS_ERR_FORMAT;
    }
    source->packet_position = physical_end;
    source->packet_cursor = cursor;
    source->packet_payload_end = payload_end;
    source->packet_payloads_remaining = payload_count;
    source->packet_payload_length_type = (uint8_t)payload_length_type;
    source->packet_property = property;
    source->packet_active = 1u;
    ++source->data_packets_read;
    ++source->packet_index;
    return 1;
}

static int asf_ensure_object_capacity(QwAudioAsfSource *source, size_t size)
{
    uint8_t *grown;
    if (size <= source->object_capacity) return QW_STATUS_OK;
    grown = (uint8_t *)realloc(source->object_data, size);
    if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
    source->object_data = grown;
    source->object_capacity = size;
    return QW_STATUS_OK;
}

static int asf_descramble_object(QwAudioAsfSource *source, size_t size)
{
    uint8_t *temporary;
    size_t offset;
    size_t packet_size = source->descramble_packet_size;
    size_t chunk_size = source->descramble_chunk_size;
    size_t span = source->descramble_span;

    if (span <= 1u) return QW_STATUS_OK;
    if (!chunk_size || packet_size % chunk_size ||
        size != packet_size * span || size % chunk_size) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    temporary = (uint8_t *)malloc(size);
    if (!temporary) return QW_STATUS_ERR_OUT_OF_MEMORY;
    for (offset = 0u; offset < size; offset += chunk_size) {
        size_t chunk_index = offset / chunk_size;
        size_t row = chunk_index / span;
        size_t column = chunk_index % span;
        size_t source_index = row + column * packet_size / chunk_size;
        size_t source_offset = source_index * chunk_size;
        if (source_offset > size || chunk_size > size - source_offset) {
            free(temporary);
            return QW_STATUS_ERR_FORMAT;
        }
        memcpy(temporary + offset, source->object_data + source_offset,
               chunk_size);
    }
    memcpy(source->object_data, temporary, size);
    free(temporary);
    return QW_STATUS_OK;
}

static int asf_prepare_object(QwAudioAsfSource *source,
                              uint64_t object_number,
                              uint64_t object_size,
                              uint32_t fragment_offset,
                              const uint8_t *fragment,
                              size_t fragment_size,
                              QwAudioAsfPacket *packet)
{
    int rc;
    if (!object_size || object_size > ASF_MAX_MEDIA_OBJECT_SIZE ||
        object_size > (uint64_t)SIZE_MAX || !fragment_size ||
        fragment_offset > object_size ||
        fragment_size > object_size - fragment_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (fragment_offset == 0u) {
        rc = asf_ensure_object_capacity(source, (size_t)object_size);
        if (rc != QW_STATUS_OK) return rc;
        source->active_object_number = object_number;
        source->object_size = object_size;
        source->object_received = 0u;
        source->object_active = 1u;
    }
    if (!source->object_active ||
        source->active_object_number != object_number ||
        source->object_size != object_size ||
        source->object_received != fragment_offset) {
        source->object_active = 0u;
        return 0;
    }
    memcpy(source->object_data + fragment_offset, fragment, fragment_size);
    source->object_received += fragment_size;
    if (source->object_received != source->object_size) return 0;
    rc = asf_descramble_object(source, (size_t)source->object_size);
    if (rc != QW_STATUS_OK) return rc;
    packet->data = source->object_data;
    packet->size = (size_t)source->object_size;
    source->object_active = 0u;
    return 1;
}

static int asf_next_compressed_subpayload(QwAudioAsfSource *source,
                                         QwAudioAsfPacket *packet)
{
    size_t cursor = source->pending_subpayload_position;
    size_t subpayload_size;
    if (cursor >= source->pending_subpayload_end) {
        source->pending_subpayload_position = 0u;
        source->pending_subpayload_end = 0u;
        source->is_compressed_payload = 0u;
        return 0;
    }
    subpayload_size = source->data[cursor++];
    if (!subpayload_size ||
        !asf_range_is_valid(cursor, subpayload_size,
                            source->pending_subpayload_end)) {
        return QW_STATUS_ERR_FORMAT;
    }
    packet->data = source->data + cursor;
    packet->size = subpayload_size;
    source->pending_subpayload_position = cursor + subpayload_size;
    return 1;
}

static int asf_parse_payload(QwAudioAsfSource *source,
                             QwAudioAsfPacket *packet)
{
    size_t cursor = source->packet_cursor;
    size_t payload_end;
    size_t payload_data_position;
    size_t replicated_end;
    uint32_t payload_length;
    uint32_t media_object_number;
    uint32_t fragment_offset;
    uint32_t replicated_length;
    uint32_t replicated_length_width;
    uint64_t object_number;
    uint64_t object_size;
    uint8_t stream_flags;
    uint8_t stream_number;
    int rc;

    if (!source->packet_payloads_remaining) {
        source->packet_active = 0u;
        return 0;
    }
    if (source->packet_payload_length_type) {
        rc = asf_read_field(source->data, source->packet_payload_end, &cursor,
                            source->packet_payload_length_type, 0u,
                            &payload_length);
        if (rc != QW_STATUS_OK || !payload_length) return QW_STATUS_ERR_FORMAT;
        if (!asf_range_is_valid(cursor, payload_length,
                                source->packet_payload_end)) return QW_STATUS_ERR_FORMAT;
        payload_end = cursor + payload_length;
    } else {
        payload_end = source->packet_payload_end;
    }
    if (!asf_range_is_valid(cursor, 1u, payload_end)) return QW_STATUS_ERR_FORMAT;
    stream_flags = source->data[cursor++];
    stream_number = stream_flags & 0x7fu;
    rc = asf_read_field(source->data, payload_end, &cursor,
                        (source->packet_property >> 4u) & 3u, 0u,
                        &media_object_number);
    if (rc != QW_STATUS_OK) return rc;
    rc = asf_read_field(source->data, payload_end, &cursor,
                        (source->packet_property >> 2u) & 3u, 0u,
                        &fragment_offset);
    if (rc != QW_STATUS_OK) return rc;
    rc = asf_read_field(source->data, payload_end, &cursor,
                        source->packet_property & 3u, 0u,
                        &replicated_length_width);
    if (rc != QW_STATUS_OK) return rc;
    replicated_length = replicated_length_width;
    if (replicated_length > payload_end - cursor) return QW_STATUS_ERR_FORMAT;
    replicated_end = cursor + replicated_length;
    payload_data_position = replicated_end;
    source->packet_cursor = payload_end;
    --source->packet_payloads_remaining;
    if (!source->packet_payloads_remaining) source->packet_active = 0u;
    if (stream_number != source->stream_number) return 0;

    object_number = media_object_number;
    if (((source->packet_property >> 4u) & 3u) == 0u) {
        object_number = source->packet_index * 64u +
                        (uint64_t)(63u - source->packet_payloads_remaining);
    }
    if (replicated_length == 1u) {
        if (replicated_end >= payload_end) return QW_STATUS_ERR_FORMAT;
        /* The single replicated byte encodes the per-sample time delta. */
        source->pending_subpayload_position = replicated_end;
        source->pending_subpayload_end = payload_end;
        source->is_compressed_payload = 1u;
        return asf_next_compressed_subpayload(source, packet);
    }
    if (replicated_length < 8u) return QW_STATUS_ERR_UNSUPPORTED;
    if (replicated_end - cursor < 8u) return QW_STATUS_ERR_FORMAT;
    object_size = asf_read_u32_le(source->data + cursor);
    if (object_size > ASF_MAX_MEDIA_OBJECT_SIZE) return QW_STATUS_ERR_UNSUPPORTED;
    if (replicated_end - cursor > 8u) {
        /* Payload extension bytes are layout metadata, not WMA frame bytes. */
    }
    if (payload_data_position >= payload_end) return QW_STATUS_ERR_FORMAT;
    return asf_prepare_object(source, object_number, object_size,
                              fragment_offset,
                              source->data + payload_data_position,
                              payload_end - payload_data_position, packet);
}

int qw_audio_asf_next_packet(QwAudioAsfSource *source,
                             QwAudioAsfPacket *packet)
{
    int rc;
    if (!source || !packet) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *packet = (QwAudioAsfPacket){0};
    for (;;) {
        if (source->is_compressed_payload) {
            rc = asf_next_compressed_subpayload(source, packet);
            if (rc != 0) return rc;
        }
        if (!source->packet_active) {
            rc = asf_parse_data_packet_header(source);
            if (rc <= 0) {
                if (rc == 0 && source->object_active) return QW_STATUS_ERR_FORMAT;
                return rc;
            }
        }
        rc = asf_parse_payload(source, packet);
        if (rc < 0) return rc;
        if (rc > 0) return rc;
    }
}

void qw_audio_asf_dispose(QwAudioAsfSource *source)
{
    if (!source) return;
    free(source->object_data);
    memset(source, 0, sizeof(*source));
}
