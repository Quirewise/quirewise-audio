#include "audio_ogg_stream_selector.h"

#include "audio_ogg_container.h"
#include "quirewise/status.h"

#include <stdlib.h>
#include <string.h>

enum { AUDIO_OGG_SIGNATURE_SIZE = 8u };

typedef struct AudioOggStreamState {
    uint32_t serial_number;
    uint32_t expected_sequence;
    size_t start_offset;
    size_t end_offset;
    uint8_t first_packet_prefix[AUDIO_OGG_SIGNATURE_SIZE];
    size_t first_packet_prefix_size;
    int packet_continues;
    int first_packet_complete;
    int is_supported_audio;
    int eos_seen;
} AudioOggStreamState;

static AudioOggStreamState *audio_ogg_find_stream(
    AudioOggStreamState *streams,
    size_t stream_count,
    uint32_t serial_number)
{
    size_t i = stream_count;
    while (i) {
        --i;
        if (streams[i].serial_number == serial_number &&
            !streams[i].eos_seen) {
            return &streams[i];
        }
    }
    return NULL;
}

static int audio_ogg_reserve_streams(AudioOggStreamState **streams,
                                    size_t *capacity,
                                    size_t required)
{
    AudioOggStreamState *grown;
    size_t next_capacity;
    if (required <= *capacity) return QW_STATUS_OK;
    next_capacity = *capacity ? *capacity : 8u;
    while (next_capacity < required) {
        if (next_capacity > SIZE_MAX / 2u) {
            next_capacity = required;
            break;
        }
        next_capacity *= 2u;
    }
    if (next_capacity > SIZE_MAX / sizeof(**streams))
        return QW_STATUS_ERR_RESOURCE_LIMIT;
    grown = (AudioOggStreamState *)realloc(
        *streams, next_capacity * sizeof(**streams));
    if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
    *streams = grown;
    *capacity = next_capacity;
    return QW_STATUS_OK;
}

static int audio_ogg_prefix_matches(const uint8_t *prefix,
                                    size_t prefix_size,
                                    const uint8_t *signature,
                                    size_t signature_size)
{
    return prefix_size >= signature_size &&
           memcmp(prefix, signature, signature_size) == 0;
}

static int audio_ogg_first_packet_is_supported_audio(
    const uint8_t *prefix,
    size_t prefix_size)
{
    static const uint8_t opus_signature[] = "OpusHead";
    static const uint8_t speex_signature[] = "Speex   ";
    static const uint8_t flac_signature[] = {0x7fu, 'F', 'L', 'A', 'C'};
    static const uint8_t flac_legacy_signature[] = {'f', 'L', 'a', 'C'};
    static const uint8_t vorbis_signature[] = {1u, 'v', 'o', 'r', 'b', 'i', 's'};
    return audio_ogg_prefix_matches(prefix, prefix_size,
                                    opus_signature, sizeof(opus_signature) - 1u) ||
           audio_ogg_prefix_matches(prefix, prefix_size,
                                    speex_signature, sizeof(speex_signature) - 1u) ||
           audio_ogg_prefix_matches(prefix, prefix_size,
                                    flac_signature, sizeof(flac_signature)) ||
           audio_ogg_prefix_matches(prefix, prefix_size,
                                    flac_legacy_signature,
                                    sizeof(flac_legacy_signature)) ||
           audio_ogg_prefix_matches(prefix, prefix_size,
                                    vorbis_signature, sizeof(vorbis_signature));
}

static int audio_ogg_scan_streams(const uint8_t *data,
                                  size_t size,
                                  AudioOggStreamState **streams,
                                  size_t *stream_capacity,
                                  size_t *stream_count,
                                  size_t *audio_track_count)
{
    size_t position = 0u;
    size_t count = 0u;
    size_t audio_count = 0u;
    while (position < size) {
        QwAudioOggPage page;
        AudioOggStreamState *stream;
        size_t body_position;
        size_t i;
        int rc = qw_audio_ogg_page_parse(data, size, position, &page);
        if (rc != QW_STATUS_OK) return rc;
        stream = audio_ogg_find_stream(*streams, count, page.serial_number);
        if (!stream) {
            if ((page.header_type & QW_AUDIO_OGG_FLAG_BOS) == 0u ||
                (page.header_type & QW_AUDIO_OGG_FLAG_CONTINUED) != 0u ||
                page.sequence_number != 0u) {
                return QW_STATUS_ERR_FORMAT;
            }
            rc = audio_ogg_reserve_streams(streams, stream_capacity,
                                           count + 1u);
            if (rc != QW_STATUS_OK) return rc;
            stream = &(*streams)[count++];
            memset(stream, 0, sizeof(*stream));
            stream->serial_number = page.serial_number;
            stream->start_offset = page.offset;
        } else if ((page.header_type & QW_AUDIO_OGG_FLAG_BOS) != 0u ||
                   stream->eos_seen ||
                   page.sequence_number != stream->expected_sequence ||
                   (((page.header_type & QW_AUDIO_OGG_FLAG_CONTINUED) != 0u) !=
                    (stream->packet_continues != 0))) {
            return QW_STATUS_ERR_FORMAT;
        }
        stream->expected_sequence = page.sequence_number + 1u;
        body_position = page.body_offset;
        for (i = 0u; i < page.segment_count; ++i) {
            size_t segment_size = page.lacing[i];
            if (!stream->first_packet_complete) {
                size_t room = AUDIO_OGG_SIGNATURE_SIZE -
                              stream->first_packet_prefix_size;
                size_t copy_size = segment_size < room ? segment_size : room;
                if (copy_size) {
                    memcpy(stream->first_packet_prefix +
                               stream->first_packet_prefix_size,
                           data + body_position, copy_size);
                    stream->first_packet_prefix_size += copy_size;
                }
                if (page.lacing[i] < 255u) {
                    stream->first_packet_complete = 1;
                    stream->is_supported_audio =
                        audio_ogg_first_packet_is_supported_audio(
                            stream->first_packet_prefix,
                            stream->first_packet_prefix_size);
                    if (stream->is_supported_audio) ++audio_count;
                }
            }
            body_position += segment_size;
        }
        if (page.segment_count) {
            stream->packet_continues =
                page.lacing[page.segment_count - 1u] == 255u;
        }
        if ((page.header_type & QW_AUDIO_OGG_FLAG_EOS) != 0u) {
            if (stream->packet_continues) return QW_STATUS_ERR_FORMAT;
            stream->eos_seen = 1;
            stream->end_offset = page.next_offset;
        }
        position = page.next_offset;
    }
    *stream_count = count;
    *audio_track_count = audio_count;
    return QW_STATUS_OK;
}

static int audio_ogg_copy_stream(const uint8_t *data,
                                 size_t size,
                                 uint32_t serial_number,
                                 size_t stream_start,
                                 size_t stream_end,
                                 uint8_t **selected_data,
                                 size_t *selected_size)
{
    size_t position = 0u;
    size_t total_size = 0u;
    uint8_t *copy;
    size_t copy_position = 0u;
    while (position < size) {
        QwAudioOggPage page;
        int rc = qw_audio_ogg_page_parse(data, size, position, &page);
        if (rc != QW_STATUS_OK) return rc;
        if (page.serial_number == serial_number &&
            page.offset >= stream_start && page.offset < stream_end) {
            size_t page_size = page.next_offset - page.offset;
            if (page_size > SIZE_MAX - total_size) return QW_STATUS_ERR_RESOURCE_LIMIT;
            total_size += page_size;
        }
        position = page.next_offset;
    }
    if (!total_size) return QW_STATUS_ERR_FORMAT;
    copy = (uint8_t *)malloc(total_size);
    if (!copy) return QW_STATUS_ERR_OUT_OF_MEMORY;
    position = 0u;
    while (position < size) {
        QwAudioOggPage page;
        size_t page_size;
        int rc = qw_audio_ogg_page_parse(data, size, position, &page);
        if (rc != QW_STATUS_OK) {
            free(copy);
            return rc;
        }
        page_size = page.next_offset - page.offset;
        if (page.serial_number == serial_number &&
            page.offset >= stream_start && page.offset < stream_end) {
            memcpy(copy + copy_position, data + page.offset, page_size);
            copy_position += page_size;
        }
        position = page.next_offset;
    }
    *selected_data = copy;
    *selected_size = total_size;
    return QW_STATUS_OK;
}

int qw_audio_ogg_select_audio_stream(const uint8_t *data,
                                     size_t size,
                                     size_t requested_audio_track,
                                     uint8_t **selected_data,
                                     size_t *selected_size,
                                     size_t *audio_track_count)
{
    AudioOggStreamState *streams = NULL;
    size_t stream_capacity = 0u;
    size_t stream_count = 0u;
    size_t audio_count = 0u;
    size_t requested_track = requested_audio_track
        ? requested_audio_track : 1u;
    size_t current_track = 0u;
    size_t i;
    int rc;
    if (!data || !size || !selected_data || !selected_size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    *selected_data = NULL;
    *selected_size = 0u;
    if (audio_track_count) *audio_track_count = 0u;
    rc = audio_ogg_scan_streams(data, size, &streams, &stream_capacity,
                                &stream_count, &audio_count);
    if (rc != QW_STATUS_OK) {
        free(streams);
        return rc;
    }
    if (audio_track_count) *audio_track_count = audio_count;
    if (!audio_count) {
        free(streams);
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (requested_track > audio_count) {
        free(streams);
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    for (i = 0u; i < stream_count; ++i) {
        if (!streams[i].is_supported_audio) continue;
        ++current_track;
        if (current_track == requested_track) {
            rc = audio_ogg_copy_stream(data, size,
                                       streams[i].serial_number,
                                       streams[i].start_offset,
                                       streams[i].end_offset
                                           ? streams[i].end_offset : size,
                                       selected_data, selected_size);
            free(streams);
            return rc;
        }
    }
    free(streams);
    return QW_STATUS_ERR_FORMAT;
}
