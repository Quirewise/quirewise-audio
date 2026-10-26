#include "audio_dff_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    DFF_ROOT_HEADER_SIZE = 16u,
    DFF_CHUNK_HEADER_SIZE = 12u,
    DFF_FORM_TYPE_SIZE = 4u
};

typedef struct QwAudioDffChunk {
    const uint8_t *id;
    size_t data_offset;
    size_t data_size;
    size_t next_offset;
} QwAudioDffChunk;

static int dff_note_unsupported(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

static int dff_read_chunk(const uint8_t *data,
                          size_t limit,
                          size_t position,
                          QwAudioDffChunk *chunk)
{
    uint64_t data_size;
    size_t padded_size;
    if (!data || !chunk || position > limit ||
        limit - position < DFF_CHUNK_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    data_size = qw_read_u64_be(data + position + 4u);
    if (data_size > (uint64_t)SIZE_MAX) return QW_STATUS_ERR_FORMAT;
    chunk->id = data + position;
    chunk->data_offset = position + DFF_CHUNK_HEADER_SIZE;
    chunk->data_size = (size_t)data_size;
    if (chunk->data_size > limit - chunk->data_offset) return QW_STATUS_ERR_FORMAT;
    padded_size = chunk->data_size + (chunk->data_size & 1u);
    if (padded_size < chunk->data_size ||
        padded_size > limit - chunk->data_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    chunk->next_offset = chunk->data_offset + padded_size;
    return QW_STATUS_OK;
}

static int dff_set_channel_mask(QwAudioDffSource *source)
{
    static const uint32_t c = UINT32_C(0x43202020);
    static const uint32_t slft = UINT32_C(0x534c4654);
    static const uint32_t srgt = UINT32_C(0x53524754);
    static const uint32_t mlft = UINT32_C(0x4d4c4654);
    static const uint32_t mrgt = UINT32_C(0x4d524754);
    static const uint32_t ls = UINT32_C(0x4c532020);
    static const uint32_t rs = UINT32_C(0x52532020);
    static const uint32_t lfe = UINT32_C(0x4c464520);
    const uint32_t *ids = source->channel_ids;

    source->channel_mask = 0u;
    if (source->channels == 1u && ids[0] == c) {
        source->channel_mask = UINT32_C(0x00000004); /* front center */
    } else if (source->channels == 2u &&
               ((ids[0] == slft && ids[1] == srgt) ||
                (ids[0] == mlft && ids[1] == mrgt))) {
        source->channel_mask = UINT32_C(0x00000003); /* front left/right */
    } else if (source->channels == 3u && ids[0] == mlft &&
               ids[1] == mrgt && ids[2] == c) {
        source->channel_mask = UINT32_C(0x00000007);
    } else if (source->channels == 4u && ids[0] == mlft &&
               ids[1] == mrgt && ids[2] == ls && ids[3] == rs) {
        source->channel_mask = UINT32_C(0x00000033); /* quad */
    } else if (source->channels == 4u && ids[0] == mlft &&
               ids[1] == mrgt && ids[2] == c && ids[3] == lfe) {
        source->channel_mask = UINT32_C(0x0000000f);
    } else if (source->channels == 5u && ids[0] == mlft &&
               ids[1] == mrgt && ids[2] == c && ids[3] == ls &&
               ids[4] == rs) {
        source->channel_mask = UINT32_C(0x00000037);
    } else if (source->channels == 6u && ids[0] == mlft &&
               ids[1] == mrgt && ids[2] == c && ids[3] == lfe &&
               ids[4] == ls && ids[5] == rs) {
        source->channel_mask = UINT32_C(0x0000003f);
    }
    return QW_STATUS_OK;
}

static int dff_parse_properties(const uint8_t *data,
                                const QwAudioDffChunk *property,
                                QwAudioDffSource *source,
                                uint32_t *sample_frequency,
                                uint32_t *compression_type,
                                QwConvertReport *report)
{
    size_t position;
    size_t end;
    int saw_frequency = 0;
    int saw_channels = 0;
    int saw_compression = 0;

    if (property->data_size < DFF_FORM_TYPE_SIZE ||
        memcmp(data + property->data_offset, "SND ", 4u) != 0) {
        return dff_note_unsupported(report,
                                    "DSDIFF property type is not sound");
    }
    position = property->data_offset + DFF_FORM_TYPE_SIZE;
    end = property->data_offset + property->data_size;
    while (position < end) {
        QwAudioDffChunk child;
        int rc = dff_read_chunk(data, end, position, &child);
        if (rc != QW_STATUS_OK) return rc;
        if (child.next_offset <= position) return QW_STATUS_ERR_FORMAT;

        if (memcmp(child.id, "FS  ", 4u) == 0) {
            if (saw_frequency || child.data_size != 4u)
                return QW_STATUS_ERR_FORMAT;
            *sample_frequency = qw_read_u32_be(data + child.data_offset);
            saw_frequency = 1;
        } else if (memcmp(child.id, "CHNL", 4u) == 0) {
            size_t expected_size;
            uint16_t channels;
            size_t channel;
            if (saw_channels || child.data_size < 2u)
                return QW_STATUS_ERR_FORMAT;
            channels = qw_read_u16_be(data + child.data_offset);
            if (!channels) return QW_STATUS_ERR_FORMAT;
            if (channels > QW_AUDIO_DFF_MAX_CHANNELS) {
                return dff_note_unsupported(
                    report, "DSDIFF channel count exceeds decoder support");
            }
            if (!qw_checked_size_mul(channels, 4u, &expected_size) ||
                !qw_checked_size_add(expected_size, 2u, &expected_size) ||
                child.data_size != expected_size) {
                return QW_STATUS_ERR_FORMAT;
            }
            source->channels = channels;
            for (channel = 0u; channel < channels; ++channel) {
                source->channel_ids[channel] = qw_read_u32_be(
                    data + child.data_offset + 2u + channel * 4u);
            }
            saw_channels = 1;
        } else if (memcmp(child.id, "CMPR", 4u) == 0) {
            uint8_t name_size;
            if (saw_compression || child.data_size < 5u)
                return QW_STATUS_ERR_FORMAT;
            name_size = data[child.data_offset + 4u];
            if (child.data_size != (size_t)name_size + 5u)
                return QW_STATUS_ERR_FORMAT;
            *compression_type = qw_read_u32_be(data + child.data_offset);
            saw_compression = 1;
        } else if (memcmp(child.id, "ID3 ", 4u) == 0 ||
                   memcmp(child.id, "ABSS", 4u) == 0 ||
                   memcmp(child.id, "LSCO", 4u) == 0 ||
                   memcmp(child.id, "DIIN", 4u) == 0 ||
                   memcmp(child.id, "COMT", 4u) == 0) {
            source->has_metadata = 1;
        } else {
            source->has_metadata = 1;
        }
        position = child.next_offset;
    }
    if (position != end || !saw_frequency || !saw_channels ||
        !saw_compression) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (*sample_frequency < 8u || *sample_frequency % 8u != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->sample_rate = *sample_frequency / 8u;
    (void)dff_set_channel_mask(source);
    return QW_STATUS_OK;
}

static int dff_parse_dst_data(const uint8_t *data,
                              size_t start,
                              size_t end,
                              QwAudioDffSource *source,
                              QwConvertReport *report)
{
    QwAudioDffChunk chunk;
    size_t position = start;
    size_t frame_count = 0u;
    size_t crc_count = 0u;
    int crc_mode = -1;
    int awaiting_crc = 0;
    uint32_t declared_frames;
    uint16_t frame_rate;

    if (position >= end || dff_read_chunk(data, end, position, &chunk) != QW_STATUS_OK ||
        memcmp(chunk.id, "FRTE", 4u) != 0 || chunk.data_size != 6u) {
        return QW_STATUS_ERR_FORMAT;
    }
    declared_frames = qw_read_u32_be(data + chunk.data_offset);
    frame_rate = qw_read_u16_be(data + chunk.data_offset + 4u);
    if (!declared_frames || frame_rate != 75u ||
        source->sample_rate % frame_rate != 0u ||
        ((uint64_t)source->sample_rate * 8u) % 44100u != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->frame_rate = frame_rate;
    source->samples_per_frame = source->sample_rate / frame_rate;
    position = chunk.next_offset;

    while (position < end) {
        int rc = dff_read_chunk(data, end, position, &chunk);
        if (rc != QW_STATUS_OK) return rc;
        if (chunk.next_offset <= position) return QW_STATUS_ERR_FORMAT;
        if (memcmp(chunk.id, "DSTF", 4u) == 0) {
            if (awaiting_crc) {
                if (crc_mode == 1) return QW_STATUS_ERR_FORMAT;
                if (crc_mode < 0) crc_mode = 0;
            }
            if (chunk.data_size < 2u || chunk.data_size > (size_t)INT_MAX)
                return QW_STATUS_ERR_FORMAT;
            if (source->frame_data_size > SIZE_MAX - chunk.data_size)
                return QW_STATUS_ERR_FORMAT;
            source->frame_data_size += chunk.data_size;
            ++frame_count;
            awaiting_crc = 1;
        } else if (memcmp(chunk.id, "DSTC", 4u) == 0) {
            if (!awaiting_crc || crc_mode == 0 || chunk.data_size != 4u)
                return QW_STATUS_ERR_FORMAT;
            crc_mode = 1;
            ++crc_count;
            awaiting_crc = 0;
        } else {
            return dff_note_unsupported(
                report, "DSDIFF DST sound data contains an unsupported local chunk");
        }
        position = chunk.next_offset;
    }
    if (position != end || (awaiting_crc && crc_mode == 1) ||
        frame_count != declared_frames || !frame_count ||
        (crc_count != 0u && crc_count != frame_count)) {
        return QW_STATUS_ERR_FORMAT;
    }
    if ((uint64_t)frame_count >
        UINT64_MAX / (uint64_t)source->samples_per_frame) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->frame_count = frame_count;
    source->sample_count =
        (uint64_t)frame_count * source->samples_per_frame;
    return QW_STATUS_OK;
}

int qw_audio_dff_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDffSource *source,
                       QwConvertReport *report)
{
    uint64_t form_size_64;
    size_t form_end;
    size_t position;
    size_t first_chunk = DFF_ROOT_HEADER_SIZE;
    uint32_t sample_frequency = 0u;
    uint32_t compression_type = 0u;
    int saw_version = 0;
    int saw_properties = 0;
    int saw_audio = 0;
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < DFF_ROOT_HEADER_SIZE || memcmp(bytes, "FRM8", 4u) != 0 ||
        memcmp(bytes + 12u, "DSD ", 4u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    form_size_64 = qw_read_u64_be(bytes + 4u);
    if (form_size_64 < DFF_FORM_TYPE_SIZE ||
        form_size_64 > (uint64_t)(size - 12u) ||
        form_size_64 > (uint64_t)SIZE_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    form_end = 12u + (size_t)form_size_64;
    source->data = bytes;
    source->size = form_end;
    position = first_chunk;

    while (position < form_end) {
        QwAudioDffChunk chunk;
        rc = dff_read_chunk(bytes, form_end, position, &chunk);
        if (rc != QW_STATUS_OK) return rc;
        if (chunk.next_offset <= position) return QW_STATUS_ERR_FORMAT;

        if (position == first_chunk && memcmp(chunk.id, "FVER", 4u) != 0) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (memcmp(chunk.id, "FVER", 4u) == 0) {
            uint32_t version;
            if (saw_version || saw_properties || saw_audio ||
                chunk.data_size != 4u) {
                return QW_STATUS_ERR_FORMAT;
            }
            version = qw_read_u32_be(bytes + chunk.data_offset);
            if ((version >> 24u) != 1u) {
                rc = dff_note_unsupported(
                    report, "native DSDIFF parser supports format version 1.x");
                return rc;
            }
            saw_version = 1;
        } else if (memcmp(chunk.id, "PROP", 4u) == 0) {
            if (!saw_version || saw_properties || saw_audio)
                return QW_STATUS_ERR_FORMAT;
            rc = dff_parse_properties(bytes, &chunk, source,
                                      &sample_frequency, &compression_type,
                                      report);
            if (rc != QW_STATUS_OK) {
                if (rc == QW_STATUS_ERR_UNSUPPORTED && report &&
                    !report->note[0]) {
                    (void)snprintf(report->note, sizeof(report->note),
                                   "native DSDIFF parser does not support this property variant");
                }
                return rc;
            }
            if (compression_type != UINT32_C(0x44534420) &&
                compression_type != UINT32_C(0x44535420)) {
                return dff_note_unsupported(
                    report, "native DSDIFF parser supports DSD and DST audio");
            }
            saw_properties = 1;
        } else if (memcmp(chunk.id, "DSD ", 4u) == 0 ||
                   memcmp(chunk.id, "DST ", 4u) == 0) {
            if (!saw_properties || saw_audio) return QW_STATUS_ERR_FORMAT;
            if (memcmp(chunk.id, "DSD ", 4u) == 0) {
                if (compression_type != UINT32_C(0x44534420)) {
                    return QW_STATUS_ERR_FORMAT;
                }
                if (chunk.data_size % source->channels != 0u)
                    return QW_STATUS_ERR_FORMAT;
                source->codec = QW_AUDIO_DFF_CODEC_DSD;
                source->audio_data_offset = chunk.data_offset;
                source->audio_data_size = chunk.data_size;
                source->audio_data_end = chunk.data_offset + chunk.data_size;
                source->sample_count = chunk.data_size / source->channels;
                if (!source->sample_count) return QW_STATUS_ERR_FORMAT;
            } else {
                size_t data_end = chunk.data_offset + chunk.data_size;
                if (compression_type != UINT32_C(0x44535420))
                    return QW_STATUS_ERR_FORMAT;
                if (source->channels > 6u) {
                    return dff_note_unsupported(
                        report, "DST decoder supports at most six channels");
                }
                source->codec = QW_AUDIO_DFF_CODEC_DST;
                source->audio_data_offset = chunk.data_offset;
                source->audio_data_size = chunk.data_size;
                source->audio_data_end = data_end;
                rc = dff_parse_dst_data(bytes, chunk.data_offset, data_end,
                                         source, report);
                if (rc != QW_STATUS_OK) {
                    if (rc == QW_STATUS_ERR_UNSUPPORTED && report &&
                        !report->note[0]) {
                        (void)snprintf(report->note, sizeof(report->note),
                                       "native DSDIFF parser does not support this DST variant");
                    }
                    return rc;
                }
            }
            saw_audio = 1;
        } else {
            source->has_metadata = 1;
        }
        position = chunk.next_offset;
    }
    if (position != form_end || !saw_version || !saw_properties || !saw_audio ||
        !source->sample_rate || !source->channels || !source->sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    return QW_STATUS_OK;
}

int qw_audio_dff_next_dst_frame(const QwAudioDffSource *source,
                                size_t position,
                                QwAudioDffFrame *frame)
{
    QwAudioDffChunk chunk;
    int rc;
    if (!source || !frame || !source->data ||
        source->codec != QW_AUDIO_DFF_CODEC_DST) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    memset(frame, 0, sizeof(*frame));
    if (position >= source->audio_data_end) {
        return position == source->audio_data_end ? 0 : QW_STATUS_ERR_FORMAT;
    }
    rc = dff_read_chunk(source->data, source->audio_data_end, position, &chunk);
    if (rc != QW_STATUS_OK) return rc;
    if (memcmp(chunk.id, "DSTF", 4u) != 0) {
        if (memcmp(chunk.id, "FRTE", 4u) != 0 &&
            memcmp(chunk.id, "DSTC", 4u) != 0) {
            return QW_STATUS_ERR_FORMAT;
        }
        return qw_audio_dff_next_dst_frame(source, chunk.next_offset, frame);
    }
    frame->data = source->data + chunk.data_offset;
    frame->size = chunk.data_size;
    frame->next_position = chunk.next_offset;
    return 1;
}
