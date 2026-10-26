#include "audio_amr_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    AMR_NB_HEADER_SIZE = 6u,
    AMR_WB_HEADER_SIZE = 9u,
    AMR_NB_FRAME_SAMPLES = 160u,
    AMR_WB_FRAME_SAMPLES = 320u
};

static const uint8_t amr_nb_magic[AMR_NB_HEADER_SIZE] = {
    '#', '!', 'A', 'M', 'R', '\n'
};
static const uint8_t amr_wb_magic[AMR_WB_HEADER_SIZE] = {
    '#', '!', 'A', 'M', 'R', '-', 'W', 'B', '\n'
};
static const uint8_t amr_nb_frame_sizes[16] = {
    13u, 14u, 16u, 18u, 20u, 21u, 27u, 32u,
    6u, 1u, 1u, 1u, 1u, 1u, 1u, 1u
};
static const uint8_t amr_wb_frame_sizes[16] = {
    18u, 24u, 33u, 37u, 41u, 47u, 51u, 59u,
    61u, 6u, 0u, 0u, 0u, 0u, 1u, 1u
};

static int amr_frame_size(int wideband,
                          const uint8_t *bytes,
                          size_t size,
                          size_t *frame_size)
{
    uint8_t frame_type;
    const uint8_t *frame_sizes = wideband
        ? amr_wb_frame_sizes : amr_nb_frame_sizes;
    if (!bytes || !frame_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!size) return QW_STATUS_ERR_FORMAT;
    frame_type = (uint8_t)((bytes[0] >> 3u) & 0x0fu);
    *frame_size = frame_sizes[frame_type];
    if (!*frame_size) return QW_STATUS_ERR_UNSUPPORTED;
    if (*frame_size > size) return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}

int qw_audio_amr_probe(const uint8_t *bytes, size_t size)
{
    return bytes &&
           ((size >= sizeof(amr_nb_magic) &&
             memcmp(bytes, amr_nb_magic, sizeof(amr_nb_magic)) == 0) ||
            (size >= sizeof(amr_wb_magic) &&
             memcmp(bytes, amr_wb_magic, sizeof(amr_wb_magic)) == 0));
}

int qw_audio_amr_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioAmrSource *source,
                       QwConvertReport *report)
{
    size_t position;
    uint64_t sample_count = 0u;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size >= sizeof(amr_nb_magic) &&
        memcmp(bytes, amr_nb_magic, sizeof(amr_nb_magic)) == 0) {
        source->first_frame_offset = sizeof(amr_nb_magic);
        source->sample_rate = 8000u;
        source->channels = 1u;
    } else if (size >= sizeof(amr_wb_magic) &&
               memcmp(bytes, amr_wb_magic, sizeof(amr_wb_magic)) == 0) {
        source->first_frame_offset = sizeof(amr_wb_magic);
        source->sample_rate = 16000u;
        source->channels = 1u;
        source->wideband = 1;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    source->data = bytes;
    source->size = size;
    source->frame_data_end = size;
    position = source->first_frame_offset;
    while (position < source->frame_data_end) {
        size_t frame_size;
        uint32_t frame_samples = source->wideband
            ? AMR_WB_FRAME_SAMPLES : AMR_NB_FRAME_SAMPLES;
        int rc = amr_frame_size(source->wideband, bytes + position,
                                source->frame_data_end - position, &frame_size);
        if (rc != QW_STATUS_OK) return rc;
        if (sample_count > UINT64_MAX - frame_samples)
            return QW_STATUS_ERR_FORMAT;
        sample_count += frame_samples;
        position += frame_size;
    }
    if (position == source->first_frame_offset) return QW_STATUS_ERR_FORMAT;
    source->sample_count = sample_count;
    return QW_STATUS_OK;
}

int qw_audio_amr_next_frame(const QwAudioAmrSource *source,
                            size_t position,
                            QwAudioAmrFrame *frame)
{
    size_t frame_size;
    uint32_t frame_samples;
    int rc;
    if (!source || !frame || !source->data)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position < source->first_frame_offset ||
        position > source->frame_data_end) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = amr_frame_size(source->wideband, source->data + position,
                        source->frame_data_end - position, &frame_size);
    if (rc != QW_STATUS_OK) return rc;
    frame_samples = source->wideband
        ? AMR_WB_FRAME_SAMPLES : AMR_NB_FRAME_SAMPLES;
    frame->data = source->data + position;
    frame->size = frame_size;
    frame->next_position = position + frame_size;
    frame->sample_count = frame_samples;
    return 1;
}
