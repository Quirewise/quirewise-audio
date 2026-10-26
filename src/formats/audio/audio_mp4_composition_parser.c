#include "audio_mp4_composition_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>

static int32_t mp4_composition_read_i32(const uint8_t *data)
{
    uint32_t raw = qw_read_u32_be(data);
    return raw <= (uint32_t)INT32_MAX
        ? (int32_t)raw
        : (int32_t)((int64_t)raw - (INT64_C(1) << 32u));
}

int qw_audio_mp4_parse_composition_offsets(const uint8_t *payload,
                                           size_t payload_size,
                                           size_t sample_count,
                                           QwAudioMp4CompositionTable *table)
{
    uint8_t version;
    uint32_t entry_count;
    uint64_t mapped_samples = 0u;
    size_t entry_index;

    if (!payload || !sample_count || !table || payload_size < 8u) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *table = (QwAudioMp4CompositionTable){0};
    if (payload[1] || payload[2] || payload[3]) return QW_STATUS_ERR_UNSUPPORTED;
    version = payload[0];
    if (version > 1u) return QW_STATUS_ERR_UNSUPPORTED;
    entry_count = qw_read_u32_be(payload + 4u);
    if (!entry_count ||
        (uint64_t)entry_count > (uint64_t)(payload_size - 8u) / 8u ||
        payload_size != 8u + (size_t)entry_count * 8u) {
        return QW_STATUS_ERR_FORMAT;
    }

    for (entry_index = 0u; entry_index < entry_count; ++entry_index) {
        const uint8_t *entry = payload + 8u + entry_index * 8u;
        uint32_t run_count = qw_read_u32_be(entry);
        if (!run_count || (uint64_t)run_count >
                              (uint64_t)sample_count - mapped_samples) {
            return QW_STATUS_ERR_FORMAT;
        }
        mapped_samples += run_count;
    }
    if (mapped_samples != (uint64_t)sample_count)
        return QW_STATUS_ERR_FORMAT;
    table->entries = payload + 8u;
    table->entry_count = entry_count;
    table->version = version;
    return QW_STATUS_OK;
}

static int64_t mp4_composition_offset(const QwAudioMp4CompositionTable *table,
                                      size_t entry_index)
{
    const uint8_t *entry = table->entries + entry_index * 8u;
    return table->version == 0u
        ? (int64_t)qw_read_u32_be(entry + 4u)
        : (int64_t)mp4_composition_read_i32(entry + 4u);
}

int qw_audio_mp4_composition_offsets_constant(
    const QwAudioMp4CompositionTable *table,
    int64_t *out_constant_offset)
{
    int64_t constant_offset;
    size_t entry_index;
    if (!table || !table->entries || !table->entry_count ||
        !out_constant_offset || table->version > 1u) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    constant_offset = mp4_composition_offset(table, 0u);
    for (entry_index = 1u; entry_index < table->entry_count; ++entry_index) {
        if (mp4_composition_offset(table, entry_index) != constant_offset)
            return QW_STATUS_ERR_UNSUPPORTED;
    }
    *out_constant_offset = constant_offset;
    return QW_STATUS_OK;
}

int qw_audio_mp4_composition_next_offset(
    const QwAudioMp4CompositionTable *table,
    QwAudioMp4CompositionCursor *cursor,
    int64_t *out_offset)
{
    if (!table || !cursor || !out_offset || !table->entries ||
        !table->entry_count || table->version > 1u) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (!cursor->samples_remaining) {
        const uint8_t *entry;
        if (cursor->entry_index >= table->entry_count)
            return QW_STATUS_ERR_FORMAT;
        entry = table->entries + cursor->entry_index * 8u;
        cursor->samples_remaining = qw_read_u32_be(entry);
        if (!cursor->samples_remaining) return QW_STATUS_ERR_FORMAT;
        cursor->current_offset = mp4_composition_offset(
            table, cursor->entry_index);
        ++cursor->entry_index;
    }
    --cursor->samples_remaining;
    *out_offset = cursor->current_offset;
    return QW_STATUS_OK;
}
