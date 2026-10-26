#include "audio_mp4_edit_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>

typedef struct {
    const uint8_t *entries;
    uint32_t entry_count;
    size_t entry_size;
    uint8_t version;
} QwAudioMp4EditListView;

typedef struct {
    uint64_t segment_duration;
    int64_t media_time;
    int32_t media_rate_integer;
    int32_t media_rate_fraction;
} QwAudioMp4EditEntry;

static int64_t mp4_edit_read_i64(const uint8_t *data)
{
    uint64_t raw = qw_read_u64_be(data);
    uint64_t magnitude;
    if (raw <= (uint64_t)INT64_MAX) return (int64_t)raw;
    magnitude = (~raw) + 1u;
    if (magnitude == (UINT64_C(1) << 63u)) return INT64_MIN;
    return -(int64_t)magnitude;
}

static int32_t mp4_edit_read_i32(const uint8_t *data)
{
    uint32_t raw = qw_read_u32_be(data);
    return raw <= (uint32_t)INT32_MAX
        ? (int32_t)raw
        : (int32_t)((int64_t)raw - (INT64_C(1) << 32u));
}

static int32_t mp4_edit_read_i16(const uint8_t *data)
{
    uint16_t raw = qw_read_u16_be(data);
    return (raw & 0x8000u)
        ? (int32_t)raw - 0x10000
        : (int32_t)raw;
}

static int mp4_edit_duration_to_samples(uint64_t duration,
                                        uint32_t movie_timescale,
                                        uint32_t sample_rate,
                                        uint64_t *out_samples)
{
    uint64_t whole_seconds;
    uint64_t remainder;
    uint64_t whole_samples;
    uint64_t fractional_samples;

    if (!movie_timescale || !sample_rate || !out_samples)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    whole_seconds = duration / movie_timescale;
    remainder = duration % movie_timescale;
    if (whole_seconds > UINT64_MAX / sample_rate)
        return QW_STATUS_ERR_FORMAT;
    whole_samples = whole_seconds * sample_rate;
    fractional_samples =
        (remainder * sample_rate + movie_timescale / 2u) / movie_timescale;
    if (fractional_samples > UINT64_MAX - whole_samples)
        return QW_STATUS_ERR_FORMAT;
    *out_samples = whole_samples + fractional_samples;
    return QW_STATUS_OK;
}

static int mp4_parse_edit_list_view(const uint8_t *payload,
                                    size_t payload_size,
                                    QwAudioMp4EditListView *view)
{
    uint32_t entry_count;
    size_t entry_size;
    if (!payload || !view || payload_size < 8u)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (payload[1] || payload[2] || payload[3]) return QW_STATUS_ERR_UNSUPPORTED;
    if (payload[0] == 0u) {
        entry_size = 12u;
    } else if (payload[0] == 1u) {
        entry_size = 20u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    entry_count = qw_read_u32_be(payload + 4u);
    if (!entry_count || (size_t)entry_count > (payload_size - 8u) / entry_size ||
        payload_size != 8u + (size_t)entry_count * entry_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    view->entries = payload + 8u;
    view->entry_count = entry_count;
    view->entry_size = entry_size;
    view->version = payload[0];
    return QW_STATUS_OK;
}

static void mp4_read_edit_entry(const QwAudioMp4EditListView *view,
                                uint32_t index,
                                QwAudioMp4EditEntry *entry)
{
    const uint8_t *data = view->entries + (size_t)index * view->entry_size;
    if (view->version == 0u) {
        entry->segment_duration = qw_read_u32_be(data);
        entry->media_time = mp4_edit_read_i32(data + 4u);
        entry->media_rate_integer = mp4_edit_read_i16(data + 8u);
        entry->media_rate_fraction = mp4_edit_read_i16(data + 10u);
    } else {
        entry->segment_duration = qw_read_u64_be(data);
        entry->media_time = mp4_edit_read_i64(data + 8u);
        entry->media_rate_integer = mp4_edit_read_i16(data + 16u);
        entry->media_rate_fraction = mp4_edit_read_i16(data + 18u);
    }
}

static int mp4_edit_list_to_sample_range(
    const QwAudioMp4EditListView *view,
    uint32_t movie_timescale,
    uint32_t media_timescale,
    uint32_t sample_rate,
    uint64_t media_sample_count,
    int64_t composition_offset,
    uint64_t *out_trim_start_samples,
    uint64_t *out_presentation_sample_count,
    uint64_t *out_leading_silence_samples)
{
    uint64_t first_media_time = 0u;
    uint64_t total_duration = 0u;
    uint64_t media_duration = 0u;
    uint64_t empty_duration = 0u;
    uint64_t total_sample_count = 0u;
    uint64_t media_sample_duration = 0u;
    uint64_t leading_silence_samples = 0u;
    uint64_t presentation_sample_count = 0u;
    uint64_t trim_start_samples;
    int media_seen = 0;
    int rc;
    uint32_t index;

    if (!view || !view->entries || !view->entry_count ||
        !out_trim_start_samples || !out_presentation_sample_count ||
        !out_leading_silence_samples) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (!movie_timescale || !sample_rate || media_timescale != sample_rate)
        return QW_STATUS_ERR_UNSUPPORTED;

    for (index = 0u; index < view->entry_count; ++index) {
        QwAudioMp4EditEntry entry;
        uint64_t expected_media_time;
        mp4_read_edit_entry(view, index, &entry);
        if (!entry.segment_duration) return QW_STATUS_ERR_UNSUPPORTED;
        if (entry.media_rate_integer != 1 || entry.media_rate_fraction != 0)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (entry.segment_duration > UINT64_MAX - total_duration)
            return QW_STATUS_ERR_FORMAT;
        total_duration += entry.segment_duration;

        if (entry.media_time == -1) {
            /* A leading empty edit becomes silence in the presentation. */
            if (media_seen) return QW_STATUS_ERR_UNSUPPORTED;
            if (index + 1u == view->entry_count) return QW_STATUS_ERR_FORMAT;
            if (entry.segment_duration > UINT64_MAX - empty_duration)
                return QW_STATUS_ERR_FORMAT;
            empty_duration += entry.segment_duration;
            continue;
        }
        if (entry.media_time < 0) return QW_STATUS_ERR_FORMAT;
        if (!media_seen) {
            first_media_time = (uint64_t)entry.media_time;
            media_seen = 1;
        } else {
            rc = mp4_edit_duration_to_samples(media_duration,
                                              movie_timescale, sample_rate,
                                              &presentation_sample_count);
            if (rc != QW_STATUS_OK) return rc;
            if (first_media_time > UINT64_MAX - presentation_sample_count)
                return QW_STATUS_ERR_FORMAT;
            expected_media_time = first_media_time + presentation_sample_count;
            if ((uint64_t)entry.media_time != expected_media_time)
                return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (entry.segment_duration > UINT64_MAX - media_duration)
            return QW_STATUS_ERR_FORMAT;
        media_duration += entry.segment_duration;
    }
    if (!media_seen) return QW_STATUS_ERR_UNSUPPORTED;

    rc = mp4_edit_duration_to_samples(total_duration, movie_timescale,
                                      sample_rate,
                                      &total_sample_count);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_edit_duration_to_samples(media_duration, movie_timescale,
                                      sample_rate, &media_sample_duration);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_edit_duration_to_samples(empty_duration, movie_timescale,
                                      sample_rate, &leading_silence_samples);
    if (rc != QW_STATUS_OK) return rc;
    if (leading_silence_samples > total_sample_count ||
        media_sample_duration != total_sample_count -
                                     leading_silence_samples) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (composition_offset > 0 && first_media_time < (uint64_t)composition_offset)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (composition_offset >= 0) {
        trim_start_samples = first_media_time -
            (uint64_t)composition_offset;
    } else {
        uint64_t offset_magnitude =
            (uint64_t)(-(composition_offset + 1)) + 1u;
        if (offset_magnitude > (uint64_t)INT64_MAX ||
            first_media_time > (uint64_t)INT64_MAX - offset_magnitude) {
            return QW_STATUS_ERR_FORMAT;
        }
        trim_start_samples = first_media_time + offset_magnitude;
    }
    if (trim_start_samples > media_sample_count ||
        !media_sample_duration ||
        media_sample_duration > media_sample_count - trim_start_samples) {
        return QW_STATUS_ERR_FORMAT;
    }
    *out_trim_start_samples = trim_start_samples;
    *out_presentation_sample_count = total_sample_count;
    *out_leading_silence_samples = leading_silence_samples;
    return QW_STATUS_OK;
}

int qw_audio_mp4_parse_edit_list(const uint8_t *payload,
                                size_t payload_size,
                                uint32_t movie_timescale,
                                uint32_t media_timescale,
                                uint32_t sample_rate,
                                uint64_t media_sample_count,
                                int64_t composition_offset,
                                uint64_t *out_trim_start_samples,
                                uint64_t *out_presentation_sample_count,
                                uint64_t *out_leading_silence_samples)
{
    QwAudioMp4EditListView view;
    int rc;

    if (!payload || !out_trim_start_samples ||
        !out_presentation_sample_count || !out_leading_silence_samples) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    rc = mp4_parse_edit_list_view(payload, payload_size, &view);
    if (rc != QW_STATUS_OK) return rc;
    return mp4_edit_list_to_sample_range(
        &view, movie_timescale, media_timescale, sample_rate,
        media_sample_count, composition_offset,
        out_trim_start_samples, out_presentation_sample_count,
        out_leading_silence_samples);
}
