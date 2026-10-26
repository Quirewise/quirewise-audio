#include "audio_mp4_fragment_parser.h"

#include "audio_mp4_parser.h"
#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MP4F_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24u) | ((uint32_t)(uint8_t)(b) << 16u) | \
     ((uint32_t)(uint8_t)(c) << 8u) | (uint32_t)(uint8_t)(d))

enum {
    MP4F_BOX_HEADER_SIZE = 8u,
    MP4F_BOX_EXTENDED_HEADER_SIZE = 16u,
    MP4F_MAX_MDAT_RANGES = 64u,
    MP4F_TFHD_BASE_DATA_OFFSET = 0x000001u,
    MP4F_TFHD_SAMPLE_DESCRIPTION_INDEX = 0x000002u,
    MP4F_TFHD_DEFAULT_SAMPLE_DURATION = 0x000008u,
    MP4F_TFHD_DEFAULT_SAMPLE_SIZE = 0x000010u,
    MP4F_TFHD_DEFAULT_SAMPLE_FLAGS = 0x000020u,
    MP4F_TFHD_DURATION_IS_EMPTY = 0x010000u,
    MP4F_TFHD_DEFAULT_BASE_IS_MOOF = 0x020000u,
    MP4F_TRUN_DATA_OFFSET = 0x000001u,
    MP4F_TRUN_FIRST_SAMPLE_FLAGS = 0x000004u,
    MP4F_TRUN_SAMPLE_DURATION = 0x000100u,
    MP4F_TRUN_SAMPLE_SIZE = 0x000200u,
    MP4F_TRUN_SAMPLE_FLAGS = 0x000400u,
    MP4F_TRUN_SAMPLE_COMPOSITION_TIME_OFFSET = 0x000800u
};

typedef struct QwMp4FragmentBox {
    uint32_t type;
    size_t offset;
    size_t payload_offset;
    size_t payload_size;
    size_t next_offset;
} QwMp4FragmentBox;

typedef struct QwMp4FragmentDataRange {
    uint64_t start;
    uint64_t end;
} QwMp4FragmentDataRange;

typedef struct QwMp4FragmentDefaults {
    uint32_t sample_description_index;
    uint32_t sample_duration;
    uint32_t sample_size;
} QwMp4FragmentDefaults;

typedef struct QwMp4FragmentTfhd {
    uint32_t track_id;
    uint32_t flags;
    uint32_t sample_description_index;
    uint32_t default_sample_duration;
    uint32_t default_sample_size;
    uint64_t base_data_offset;
} QwMp4FragmentTfhd;

static int mp4f_read_box(const uint8_t *data,
                         size_t limit,
                         size_t position,
                         QwMp4FragmentBox *box)
{
    uint64_t box_size;
    size_t header_size = MP4F_BOX_HEADER_SIZE;
    uint32_t size32;
    if (!data || !box || position > limit ||
        limit - position < MP4F_BOX_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    size32 = qw_read_u32_be(data + position);
    box->type = qw_read_u32_be(data + position + 4u);
    if (size32 == 1u) {
        if (limit - position < MP4F_BOX_EXTENDED_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        box_size = qw_read_u64_be(data + position + MP4F_BOX_HEADER_SIZE);
        header_size = MP4F_BOX_EXTENDED_HEADER_SIZE;
    } else if (size32 == 0u) {
        box_size = (uint64_t)(limit - position);
    } else {
        box_size = size32;
    }
    if (box_size < header_size || box_size > (uint64_t)(limit - position) ||
        box_size > (uint64_t)SIZE_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    box->offset = position;
    box->payload_offset = position + header_size;
    box->payload_size = (size_t)box_size - header_size;
    box->next_offset = position + (size_t)box_size;
    return QW_STATUS_OK;
}

static int mp4f_find_child(const uint8_t *data,
                           const QwMp4FragmentBox *parent,
                           uint32_t type,
                           QwMp4FragmentBox *child,
                           int *found)
{
    size_t position;
    size_t end;
    int matches = 0;
    if (!data || !parent || !child || !found ||
        parent->payload_offset > SIZE_MAX - parent->payload_size) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    position = parent->payload_offset;
    end = parent->payload_offset + parent->payload_size;
    memset(child, 0, sizeof(*child));
    while (position < end) {
        QwMp4FragmentBox current;
        int rc = mp4f_read_box(data, end, position, &current);
        if (rc != QW_STATUS_OK || current.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (current.type == type) {
            if (++matches > 1) return QW_STATUS_ERR_FORMAT;
            *child = current;
        }
        position = current.next_offset;
    }
    *found = matches != 0;
    return QW_STATUS_OK;
}

static int32_t mp4f_read_i32(const uint8_t *data)
{
    uint32_t raw = qw_read_u32_be(data);
    return raw <= (uint32_t)INT32_MAX
        ? (int32_t)raw
        : (int32_t)((int64_t)raw - (INT64_C(1) << 32u));
}

static int mp4f_parse_trex(const uint8_t *data,
                           const QwMp4FragmentBox *moov,
                           uint32_t track_id,
                           QwMp4FragmentDefaults *defaults,
                           int *found)
{
    QwMp4FragmentBox mvex;
    size_t position;
    size_t end;
    int found_mvex;
    int rc;
    if (!defaults || !found) return QW_STATUS_ERR_INVALID_ARGUMENT;
    *defaults = (QwMp4FragmentDefaults){0};
    *found = 0;
    rc = mp4f_find_child(data, moov, MP4F_FOURCC('m', 'v', 'e', 'x'),
                         &mvex, &found_mvex);
    if (rc != QW_STATUS_OK || !found_mvex) return rc;
    position = mvex.payload_offset;
    end = mvex.payload_offset + mvex.payload_size;
    while (position < end) {
        QwMp4FragmentBox box;
        rc = mp4f_read_box(data, end, position, &box);
        if (rc != QW_STATUS_OK || box.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (box.type == MP4F_FOURCC('t', 'r', 'e', 'x')) {
            const uint8_t *payload = data + box.payload_offset;
            if (box.payload_size != 24u || payload[0] != 0u ||
                payload[1] || payload[2] || payload[3]) {
                return QW_STATUS_ERR_FORMAT;
            }
            if (qw_read_u32_be(payload + 4u) == track_id) {
                if (*found) return QW_STATUS_ERR_FORMAT;
                defaults->sample_description_index =
                    qw_read_u32_be(payload + 8u);
                defaults->sample_duration = qw_read_u32_be(payload + 12u);
                defaults->sample_size = qw_read_u32_be(payload + 16u);
                *found = 1;
            }
        }
        position = box.next_offset;
    }
    return QW_STATUS_OK;
}

static int mp4f_parse_tfhd(const uint8_t *data,
                           const QwMp4FragmentBox *tfhd,
                           QwMp4FragmentTfhd *parsed)
{
    const uint8_t *payload;
    size_t position = 8u;
    uint32_t allowed_flags = MP4F_TFHD_BASE_DATA_OFFSET |
        MP4F_TFHD_SAMPLE_DESCRIPTION_INDEX |
        MP4F_TFHD_DEFAULT_SAMPLE_DURATION |
        MP4F_TFHD_DEFAULT_SAMPLE_SIZE |
        MP4F_TFHD_DEFAULT_SAMPLE_FLAGS |
        MP4F_TFHD_DURATION_IS_EMPTY |
        MP4F_TFHD_DEFAULT_BASE_IS_MOOF;
    if (!data || !tfhd || !parsed || tfhd->payload_size < 8u)
        return QW_STATUS_ERR_FORMAT;
    payload = data + tfhd->payload_offset;
    parsed->flags = ((uint32_t)payload[1] << 16u) |
                    ((uint32_t)payload[2] << 8u) | payload[3];
    if (payload[0] != 0u || (parsed->flags & ~allowed_flags) != 0u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if ((parsed->flags & (MP4F_TFHD_BASE_DATA_OFFSET |
                          MP4F_TFHD_DEFAULT_BASE_IS_MOOF)) ==
        (MP4F_TFHD_BASE_DATA_OFFSET | MP4F_TFHD_DEFAULT_BASE_IS_MOOF)) {
        return QW_STATUS_ERR_FORMAT;
    }
    parsed->track_id = qw_read_u32_be(payload + 4u);
    parsed->sample_description_index = 0u;
    parsed->default_sample_duration = 0u;
    parsed->default_sample_size = 0u;
    parsed->base_data_offset = 0u;
    if (!parsed->track_id) return QW_STATUS_ERR_FORMAT;
    if (parsed->flags & MP4F_TFHD_BASE_DATA_OFFSET) {
        if (tfhd->payload_size - position < 8u) return QW_STATUS_ERR_FORMAT;
        parsed->base_data_offset = qw_read_u64_be(payload + position);
        position += 8u;
    }
    if (parsed->flags & MP4F_TFHD_SAMPLE_DESCRIPTION_INDEX) {
        if (tfhd->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        parsed->sample_description_index = qw_read_u32_be(payload + position);
        position += 4u;
    }
    if (parsed->flags & MP4F_TFHD_DEFAULT_SAMPLE_DURATION) {
        if (tfhd->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        parsed->default_sample_duration = qw_read_u32_be(payload + position);
        position += 4u;
    }
    if (parsed->flags & MP4F_TFHD_DEFAULT_SAMPLE_SIZE) {
        if (tfhd->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        parsed->default_sample_size = qw_read_u32_be(payload + position);
        position += 4u;
    }
    if (parsed->flags & MP4F_TFHD_DEFAULT_SAMPLE_FLAGS) {
        if (tfhd->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        position += 4u;
    }
    return position == tfhd->payload_size ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int mp4f_parse_tfdt(const uint8_t *data,
                           const QwMp4FragmentBox *tfdt,
                           uint64_t *decode_time)
{
    const uint8_t *payload;
    if (!data || !tfdt || !decode_time || tfdt->payload_size < 4u)
        return QW_STATUS_ERR_FORMAT;
    payload = data + tfdt->payload_offset;
    if (payload[1] || payload[2] || payload[3]) return QW_STATUS_ERR_UNSUPPORTED;
    if (payload[0] == 0u && tfdt->payload_size == 8u) {
        *decode_time = qw_read_u32_be(payload + 4u);
        return QW_STATUS_OK;
    }
    if (payload[0] == 1u && tfdt->payload_size == 12u) {
        *decode_time = qw_read_u64_be(payload + 4u);
        return QW_STATUS_OK;
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

static int mp4f_append_sample(QwAudioMp4FragmentIndex *index,
                              const QwAudioMp4FragmentSample *sample)
{
    QwAudioMp4FragmentSample *grown;
    size_t capacity;
    if (index->sample_count >= QW_AUDIO_MP4_MAX_SAMPLES)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (index->sample_count == index->sample_capacity) {
        if (index->sample_capacity >= QW_AUDIO_MP4_MAX_SAMPLES)
            return QW_STATUS_ERR_UNSUPPORTED;
        capacity = index->sample_capacity ? index->sample_capacity * 2u : 256u;
        if (capacity > QW_AUDIO_MP4_MAX_SAMPLES)
            capacity = QW_AUDIO_MP4_MAX_SAMPLES;
        if (capacity > SIZE_MAX / sizeof(*index->samples))
            return QW_STATUS_ERR_OUT_OF_MEMORY;
        grown = (QwAudioMp4FragmentSample *)realloc(
            index->samples, capacity * sizeof(*index->samples));
        if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
        index->samples = grown;
        index->sample_capacity = capacity;
    }
    index->samples[index->sample_count++] = *sample;
    return QW_STATUS_OK;
}

static int mp4f_offset_in_mdat(uint64_t offset,
                               uint32_t sample_size,
                               const QwMp4FragmentDataRange *ranges,
                               size_t range_count)
{
    uint64_t end;
    size_t i;
    if (offset > UINT64_MAX - sample_size) return 0;
    end = offset + sample_size;
    for (i = 0u; i < range_count; ++i) {
        if (offset >= ranges[i].start && end <= ranges[i].end) return 1;
    }
    return 0;
}

static int mp4f_resolve_run_start(uint64_t base,
                                  int has_data_offset,
                                  int32_t data_offset,
                                  int has_previous_run,
                                  uint64_t previous_run_end,
                                  uint64_t *run_start)
{
    if (!run_start) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!has_data_offset) {
        *run_start = has_previous_run ? previous_run_end : base;
        return QW_STATUS_OK;
    }
    if (data_offset >= 0) {
        if (base > UINT64_MAX - (uint32_t)data_offset)
            return QW_STATUS_ERR_FORMAT;
        *run_start = base + (uint32_t)data_offset;
        return QW_STATUS_OK;
    }
    {
        uint64_t magnitude = (uint64_t)(-(int64_t)data_offset);
        if (base < magnitude) return QW_STATUS_ERR_FORMAT;
        *run_start = base - magnitude;
    }
    return QW_STATUS_OK;
}

static int mp4f_parse_trun(const uint8_t *data,
                           const QwMp4FragmentBox *trun,
                           uint64_t base_data_offset,
                           int has_previous_run,
                           uint64_t previous_run_end,
                           uint32_t default_duration,
                           uint32_t default_size,
                           uint64_t *decode_time,
                           const QwMp4FragmentDataRange *mdat_ranges,
                           size_t mdat_range_count,
                           QwAudioMp4FragmentIndex *index,
                           uint64_t *run_end)
{
    const uint8_t *payload;
    uint8_t version;
    uint32_t flags;
    uint32_t sample_count;
    uint32_t allowed_flags = MP4F_TRUN_DATA_OFFSET |
        MP4F_TRUN_FIRST_SAMPLE_FLAGS | MP4F_TRUN_SAMPLE_DURATION |
        MP4F_TRUN_SAMPLE_SIZE | MP4F_TRUN_SAMPLE_FLAGS |
        MP4F_TRUN_SAMPLE_COMPOSITION_TIME_OFFSET;
    size_t position = 8u;
    int32_t data_offset = 0;
    int has_data_offset;
    uint64_t sample_offset;
    uint32_t i;
    int rc;
    if (!data || !trun || !decode_time || !index || !run_end ||
        trun->payload_size < 8u) {
        return QW_STATUS_ERR_FORMAT;
    }
    payload = data + trun->payload_offset;
    version = payload[0];
    flags = ((uint32_t)payload[1] << 16u) |
            ((uint32_t)payload[2] << 8u) | payload[3];
    if (version > 1u || (flags & ~allowed_flags) != 0u)
        return QW_STATUS_ERR_UNSUPPORTED;
    if ((flags & (MP4F_TRUN_FIRST_SAMPLE_FLAGS |
                  MP4F_TRUN_SAMPLE_FLAGS)) ==
        (MP4F_TRUN_FIRST_SAMPLE_FLAGS | MP4F_TRUN_SAMPLE_FLAGS)) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_count = qw_read_u32_be(payload + 4u);
    if ((uint64_t)sample_count >
        (uint64_t)QW_AUDIO_MP4_MAX_SAMPLES - index->sample_count) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    has_data_offset = (flags & MP4F_TRUN_DATA_OFFSET) != 0u;
    if (has_data_offset) {
        if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        data_offset = mp4f_read_i32(payload + position);
        position += 4u;
    }
    if (flags & MP4F_TRUN_FIRST_SAMPLE_FLAGS) {
        if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
        position += 4u;
    }
    rc = mp4f_resolve_run_start(base_data_offset, has_data_offset,
                                data_offset, has_previous_run,
                                previous_run_end, &sample_offset);
    if (rc != QW_STATUS_OK) return rc;
    for (i = 0u; i < sample_count; ++i) {
        QwAudioMp4FragmentSample sample;
        uint32_t duration = default_duration;
        uint32_t sample_size = default_size;
        int64_t composition_offset = 0;
        int64_t presentation_time;
        if (flags & MP4F_TRUN_SAMPLE_DURATION) {
            if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
            duration = qw_read_u32_be(payload + position);
            position += 4u;
        }
        if (flags & MP4F_TRUN_SAMPLE_SIZE) {
            if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
            sample_size = qw_read_u32_be(payload + position);
            position += 4u;
        }
        if (flags & MP4F_TRUN_SAMPLE_FLAGS) {
            if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
            position += 4u;
        }
        if (flags & MP4F_TRUN_SAMPLE_COMPOSITION_TIME_OFFSET) {
            uint32_t raw;
            if (trun->payload_size - position < 4u) return QW_STATUS_ERR_FORMAT;
            raw = qw_read_u32_be(payload + position);
            composition_offset = version == 0u
                ? (int64_t)raw : (int64_t)mp4f_read_i32(payload + position);
            position += 4u;
        }
        if (!duration || !sample_size || sample_size > (uint32_t)INT_MAX ||
            *decode_time > (uint64_t)INT64_MAX ||
            !mp4f_offset_in_mdat(sample_offset, sample_size,
                                 mdat_ranges, mdat_range_count)) {
            return QW_STATUS_ERR_FORMAT;
        }
        presentation_time = (int64_t)*decode_time;
        if ((composition_offset > 0 &&
             presentation_time > INT64_MAX - composition_offset) ||
            (composition_offset < 0 &&
             presentation_time < INT64_MIN - composition_offset)) {
            return QW_STATUS_ERR_FORMAT;
        }
        presentation_time += composition_offset;
        if (*decode_time > UINT64_MAX - duration ||
            index->decoded_duration > (uint64_t)INT64_MAX - duration ||
            sample_offset > UINT64_MAX - sample_size ||
            index->total_sample_bytes > UINT64_MAX - sample_size) {
            return QW_STATUS_ERR_FORMAT;
        }
        sample.offset = (size_t)sample_offset;
        if ((uint64_t)sample.offset != sample_offset) return QW_STATUS_ERR_FORMAT;
        sample.no_pts = presentation_time < 0;
        sample.pts = presentation_time < 0 ? 0u : (uint64_t)presentation_time;
        sample.size = sample_size;
        sample.duration = duration;
        rc = mp4f_append_sample(index, &sample);
        if (rc != QW_STATUS_OK) return rc;
        index->total_sample_bytes += sample_size;
        index->decoded_duration += duration;
        *decode_time += duration;
        sample_offset += sample_size;
    }
    if (position != trun->payload_size) return QW_STATUS_ERR_FORMAT;
    *run_end = sample_offset;
    return QW_STATUS_OK;
}

static int mp4f_parse_traf(const uint8_t *data,
                           const QwMp4FragmentBox *moof,
                           const QwMp4FragmentBox *traf,
                           size_t traf_count,
                           uint32_t target_track_id,
                           uint32_t sample_description_count,
                           const QwMp4FragmentDefaults *trex,
                           uint64_t *track_decode_time,
                           const QwMp4FragmentDataRange *mdat_ranges,
                           size_t mdat_range_count,
                           QwAudioMp4FragmentIndex *index,
                           int *is_target)
{
    QwMp4FragmentBox tfhd_box;
    QwMp4FragmentBox tfdt_box;
    QwMp4FragmentTfhd tfhd;
    uint64_t decode_time;
    uint64_t base_data_offset;
    uint64_t previous_run_end = 0u;
    uint32_t sample_description_index;
    size_t position;
    size_t end;
    int found_tfhd;
    int found_tfdt;
    int has_previous_run = 0;
    int rc;
    if (!data || !moof || !traf || !trex || !track_decode_time ||
        !index || !is_target) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *is_target = 0;
    rc = mp4f_find_child(data, traf, MP4F_FOURCC('t', 'f', 'h', 'd'),
                         &tfhd_box, &found_tfhd);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_tfhd) return QW_STATUS_ERR_FORMAT;
    rc = mp4f_parse_tfhd(data, &tfhd_box, &tfhd);
    if (rc != QW_STATUS_OK) return rc;
    if (tfhd.track_id != target_track_id) return QW_STATUS_OK;
    *is_target = 1;
    if (tfhd.flags & MP4F_TFHD_DURATION_IS_EMPTY)
        return QW_STATUS_ERR_UNSUPPORTED;

    position = traf->payload_offset;
    end = traf->payload_offset + traf->payload_size;
    while (position < end) {
        QwMp4FragmentBox box;
        rc = mp4f_read_box(data, end, position, &box);
        if (rc != QW_STATUS_OK || box.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (box.type == MP4F_FOURCC('s', 'e', 'n', 'c') ||
            box.type == MP4F_FOURCC('s', 'a', 'i', 'z') ||
            box.type == MP4F_FOURCC('s', 'a', 'i', 'o')) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        position = box.next_offset;
    }
    rc = mp4f_find_child(data, traf, MP4F_FOURCC('t', 'f', 'd', 't'),
                         &tfdt_box, &found_tfdt);
    if (rc != QW_STATUS_OK) return rc;
    decode_time = *track_decode_time;
    if (found_tfdt) {
        rc = mp4f_parse_tfdt(data, &tfdt_box, &decode_time);
        if (rc != QW_STATUS_OK) return rc;
    }

    if (tfhd.flags & MP4F_TFHD_BASE_DATA_OFFSET) {
        base_data_offset = tfhd.base_data_offset;
    } else if (tfhd.flags & MP4F_TFHD_DEFAULT_BASE_IS_MOOF) {
        base_data_offset = moof->offset;
    } else if (traf_count == 1u) {
        base_data_offset = moof->offset;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    sample_description_index =
        (tfhd.flags & MP4F_TFHD_SAMPLE_DESCRIPTION_INDEX)
            ? tfhd.sample_description_index
            : trex->sample_description_index;
    if (!sample_description_index && sample_description_count == 1u)
        sample_description_index = 1u;
    if (!sample_description_index ||
        sample_description_index > sample_description_count) {
        return QW_STATUS_ERR_FORMAT;
    }

    position = traf->payload_offset;
    end = traf->payload_offset + traf->payload_size;
    while (position < end) {
        QwMp4FragmentBox box;
        if (mp4f_read_box(data, end, position, &box) != QW_STATUS_OK ||
            box.next_offset <= position) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (box.type == MP4F_FOURCC('t', 'r', 'u', 'n')) {
            uint64_t run_end;
            uint32_t default_duration =
                (tfhd.flags & MP4F_TFHD_DEFAULT_SAMPLE_DURATION)
                    ? tfhd.default_sample_duration : trex->sample_duration;
            uint32_t default_size =
                (tfhd.flags & MP4F_TFHD_DEFAULT_SAMPLE_SIZE)
                    ? tfhd.default_sample_size : trex->sample_size;
            rc = mp4f_parse_trun(data, &box, base_data_offset,
                                 has_previous_run, previous_run_end,
                                 default_duration, default_size, &decode_time,
                                 mdat_ranges, mdat_range_count, index,
                                 &run_end);
            if (rc != QW_STATUS_OK) return rc;
            previous_run_end = run_end;
            has_previous_run = 1;
        }
        position = box.next_offset;
    }
    *track_decode_time = decode_time;
    return QW_STATUS_OK;
}

static int mp4f_parse_moof(const uint8_t *data,
                           const QwMp4FragmentBox *moof,
                           uint32_t target_track_id,
                           uint32_t sample_description_count,
                           const QwMp4FragmentDefaults *trex,
                           uint64_t *track_decode_time,
                           const QwMp4FragmentDataRange *mdat_ranges,
                           size_t mdat_range_count,
                           QwAudioMp4FragmentIndex *index)
{
    size_t position = moof->payload_offset;
    size_t end = moof->payload_offset + moof->payload_size;
    size_t traf_count = 0u;
    size_t selected_traf_count = 0u;
    int rc;
    while (position < end) {
        QwMp4FragmentBox box;
        if (mp4f_read_box(data, end, position, &box) != QW_STATUS_OK ||
            box.next_offset <= position) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (box.type == MP4F_FOURCC('t', 'r', 'a', 'f')) ++traf_count;
        position = box.next_offset;
    }
    position = moof->payload_offset;
    while (position < end) {
        QwMp4FragmentBox box;
        if (mp4f_read_box(data, end, position, &box) != QW_STATUS_OK ||
            box.next_offset <= position) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (box.type == MP4F_FOURCC('t', 'r', 'a', 'f')) {
            int is_target;
            rc = mp4f_parse_traf(data, moof, &box, traf_count,
                                 target_track_id, sample_description_count,
                                 trex, track_decode_time, mdat_ranges,
                                 mdat_range_count, index, &is_target);
            if (rc != QW_STATUS_OK) return rc;
            if (is_target && ++selected_traf_count > 1u)
                return QW_STATUS_ERR_FORMAT;
        }
        position = box.next_offset;
    }
    return QW_STATUS_OK;
}

void qw_audio_mp4_fragment_index_dispose(QwAudioMp4FragmentIndex *index)
{
    if (!index) return;
    free(index->samples);
    memset(index, 0, sizeof(*index));
}

int qw_audio_mp4_parse_fragments(const uint8_t *data,
                                 size_t size,
                                 uint32_t track_id,
                                 uint32_t sample_description_count,
                                 QwAudioMp4FragmentIndex *index)
{
    QwMp4FragmentBox moov;
    QwMp4FragmentDataRange mdat_ranges[MP4F_MAX_MDAT_RANGES];
    QwMp4FragmentDefaults trex = {0};
    uint64_t decode_time = 0u;
    size_t mdat_range_count = 0u;
    size_t moov_count = 0u;
    size_t moof_count = 0u;
    size_t position = 0u;
    int found_trex;
    int rc;
    if (!data || !size || !track_id || !sample_description_count || !index)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(index, 0, sizeof(*index));
    memset(&moov, 0, sizeof(moov));
    while (position < size) {
        QwMp4FragmentBox box;
        rc = mp4f_read_box(data, size, position, &box);
        if (rc != QW_STATUS_OK || box.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (box.type == MP4F_FOURCC('m', 'o', 'o', 'v')) {
            if (++moov_count > 1u) return QW_STATUS_ERR_UNSUPPORTED;
            moov = box;
        } else if (box.type == MP4F_FOURCC('m', 'd', 'a', 't')) {
            if (mdat_range_count >= MP4F_MAX_MDAT_RANGES)
                return QW_STATUS_ERR_UNSUPPORTED;
            mdat_ranges[mdat_range_count].start = box.payload_offset;
            mdat_ranges[mdat_range_count].end = box.next_offset;
            ++mdat_range_count;
        } else if (box.type == MP4F_FOURCC('m', 'o', 'o', 'f')) {
            ++moof_count;
        }
        position = box.next_offset;
    }
    if (!moov_count || !moof_count || !mdat_range_count)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = mp4f_parse_trex(data, &moov, track_id, &trex, &found_trex);
    if (rc != QW_STATUS_OK) return rc;
    (void)found_trex;

    position = 0u;
    while (position < size) {
        QwMp4FragmentBox box;
        rc = mp4f_read_box(data, size, position, &box);
        if (rc != QW_STATUS_OK || box.next_offset <= position) {
            rc = QW_STATUS_ERR_FORMAT;
            goto fail;
        }
        if (box.type == MP4F_FOURCC('m', 'o', 'o', 'f')) {
            rc = mp4f_parse_moof(data, &box, track_id,
                                 sample_description_count, &trex,
                                 &decode_time, mdat_ranges, mdat_range_count,
                                 index);
            if (rc != QW_STATUS_OK) goto fail;
        }
        position = box.next_offset;
    }
    if (!index->sample_count) {
        rc = QW_STATUS_ERR_UNSUPPORTED;
        goto fail;
    }
    return QW_STATUS_OK;

fail:
    qw_audio_mp4_fragment_index_dispose(index);
    return rc;
}
