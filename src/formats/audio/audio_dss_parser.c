#include "audio_dss_parser.h"

#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    DSS_CODEC_OFFSET = 0x2a4u,
    DSS_MIN_HEADER_VERSION = 2u,
    DSS_MAX_HEADER_VERSION = 3u
};

static int dss_note_unsupported(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

static size_t dss_g723_frame_size(uint8_t header)
{
    static const uint8_t frame_sizes[4] = {24u, 20u, 4u, 1u};
    return frame_sizes[header & 3u];
}

static int dss_set_sample_count(QwAudioDssSource *source)
{
    if (source->frame_count > UINT64_MAX / source->samples_per_frame) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->sample_count =
        (uint64_t)source->frame_count * source->samples_per_frame;
    return QW_STATUS_OK;
}

int qw_audio_dss_payload_read(const QwAudioDssSource *source,
                              size_t position,
                              uint8_t *output,
                              size_t size)
{
    size_t remaining;
    if (!source || (!output && size) ||
        source->audio_data_offset > source->size ||
        source->audio_data_size > source->size - source->audio_data_offset ||
        position > source->payload_size ||
        size > source->payload_size - position) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    remaining = size;
    while (remaining) {
        size_t block_index = position / QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE;
        size_t block_position = position % QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE;
        size_t chunk_size =
            QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE - block_position;
        size_t block_offset;
        size_t source_offset;
        if (block_index >= source->block_count ||
            block_index > SIZE_MAX / QW_AUDIO_DSS_BLOCK_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (chunk_size > remaining) chunk_size = remaining;
        block_offset = block_index * QW_AUDIO_DSS_BLOCK_SIZE;
        if (block_offset > source->audio_data_size ||
            QW_AUDIO_DSS_BLOCK_HEADER_SIZE >
                source->audio_data_size - block_offset ||
            block_position > source->audio_data_size - block_offset -
                                 QW_AUDIO_DSS_BLOCK_HEADER_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        source_offset = source->audio_data_offset + block_offset +
                        QW_AUDIO_DSS_BLOCK_HEADER_SIZE + block_position;
        if (chunk_size > source->size - source_offset) {
            return QW_STATUS_ERR_FORMAT;
        }
        memcpy(output, source->data + source_offset, chunk_size);
        output += chunk_size;
        position += chunk_size;
        remaining -= chunk_size;
    }
    return QW_STATUS_OK;
}

int qw_audio_dss_next_g723_frame(const QwAudioDssSource *source,
                                 size_t position,
                                 QwAudioDssFrame *frame)
{
    uint8_t header;
    size_t frame_size;
    int rc;
    if (!source || !frame || source->codec != QW_AUDIO_DSS_CODEC_G723_1) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (position >= source->payload_size) return 0;
    rc = qw_audio_dss_payload_read(source, position, &header, 1u);
    if (rc != QW_STATUS_OK) return rc;
    if (header == 0xffu) return QW_STATUS_ERR_FORMAT;
    frame_size = dss_g723_frame_size(header);
    if (frame_size > source->payload_size - position) return 0;
    frame->size = frame_size;
    frame->next_position = position + frame_size;
    return 1;
}

int qw_audio_dss_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDssSource *source,
                       QwConvertReport *report)
{
    size_t header_size;
    size_t audio_data_size;
    size_t frame_position = 0u;
    uint8_t version;
    uint8_t codec_type;
    int rc;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < 4u || (bytes[0] != DSS_MIN_HEADER_VERSION &&
                      bytes[0] != DSS_MAX_HEADER_VERSION) ||
        memcmp(bytes + 1u, "dss", 3u) != 0) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    version = bytes[0];
    header_size = (size_t)version * QW_AUDIO_DSS_BLOCK_SIZE;
    if (size < header_size || size - header_size <
                                  QW_AUDIO_DSS_BLOCK_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    codec_type = bytes[DSS_CODEC_OFFSET];
    if (codec_type == 0u) {
        source->codec = QW_AUDIO_DSS_CODEC_SP;
        source->sample_rate = 11025u;
        source->samples_per_frame = 264u;
    } else if (codec_type == 2u) {
        source->codec = QW_AUDIO_DSS_CODEC_G723_1;
        source->sample_rate = 8000u;
        source->samples_per_frame = 240u;
    } else {
        return dss_note_unsupported(report,
                                    "DSS codec type is not supported");
    }

    audio_data_size = size - header_size;
    if (audio_data_size % QW_AUDIO_DSS_BLOCK_SIZE != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->data = bytes;
    source->size = size;
    source->audio_data_offset = header_size;
    source->audio_data_size = audio_data_size;
    source->block_count = audio_data_size / QW_AUDIO_DSS_BLOCK_SIZE;
    if (source->block_count >
        SIZE_MAX / QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->payload_size =
        source->block_count * QW_AUDIO_DSS_BLOCK_PAYLOAD_SIZE;
    source->channels = 1u;
    source->has_metadata = 1;

    if (source->codec == QW_AUDIO_DSS_CODEC_SP) {
        size_t complete_pairs = source->payload_size / 82u;
        size_t trailing_bytes = source->payload_size % 82u;
        source->frame_count = complete_pairs * 2u;
        if (trailing_bytes >= QW_AUDIO_DSS_SP_PACKET_SIZE) {
            ++source->frame_count;
        }
    } else {
        for (;;) {
            QwAudioDssFrame frame;
            rc = qw_audio_dss_next_g723_frame(source, frame_position,
                                               &frame);
            if (rc < 0) return rc;
            if (rc == 0) break;
            if (source->frame_count == SIZE_MAX) return QW_STATUS_ERR_FORMAT;
            ++source->frame_count;
            frame_position = frame.next_position;
        }
    }
    return dss_set_sample_count(source);
}
