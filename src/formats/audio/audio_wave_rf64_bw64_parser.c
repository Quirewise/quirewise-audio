#include "audio_wave_rf64_bw64_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    RF64_RIFF_HEADER_SIZE = 12u,
    RF64_CHUNK_HEADER_SIZE = 8u,
    RF64_DS64_MIN_SIZE = 28u,
    RF64_DS64_ENTRY_SIZE = 12u
};

typedef struct QwRf64TableEntry {
    uint32_t chunk_id;
    uint64_t chunk_size;
    int used;
} QwRf64TableEntry;

static int rf64_resolve_chunk_size(uint32_t chunk_id,
                                   uint32_t size32,
                                   uint64_t data_size64,
                                   int *data_size_used,
                                   QwRf64TableEntry *table,
                                   size_t table_count,
                                   uint64_t *resolved_size)
{
    size_t i;

    if (!data_size_used || !resolved_size) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size32 != UINT32_MAX) {
        *resolved_size = size32;
        return QW_STATUS_OK;
    }
    if (chunk_id == UINT32_C(0x61746164) && !*data_size_used) {
        *resolved_size = data_size64;
        *data_size_used = 1;
        return QW_STATUS_OK;
    }
    for (i = 0u; i < table_count; ++i) {
        if (!table[i].used && table[i].chunk_id == chunk_id) {
            table[i].used = 1;
            *resolved_size = table[i].chunk_size;
            return QW_STATUS_OK;
        }
    }
    return QW_STATUS_ERR_FORMAT;
}

int qw_audio_wave_rf64_bw64_parse_chunks(const uint8_t *bytes,
                                         size_t size,
                                         QwAudioWaveChunks *chunks,
                                         QwConvertReport *report)
{
    QwRf64TableEntry *table = NULL;
    uint32_t riff_size32;
    uint32_t ds64_size32;
    uint32_t table_count32;
    uint64_t riff_size64;
    uint64_t data_size64;
    uint64_t sample_count64;
    uint64_t effective_riff_size;
    size_t ds64_size;
    size_t ds64_end;
    size_t riff_end;
    size_t offset;
    size_t table_count;
    size_t i;
    int data_size_used = 0;
    int format_seen = 0;
    int is_rf64;
    int is_bw64;
    int rc = QW_STATUS_ERR_FORMAT;

    if (!bytes || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(chunks, 0, sizeof(*chunks));
    is_rf64 = size >= 4u && memcmp(bytes, "RF64", 4u) == 0;
    is_bw64 = size >= 4u && memcmp(bytes, "BW64", 4u) == 0;
    if (size < RF64_RIFF_HEADER_SIZE + RF64_CHUNK_HEADER_SIZE +
                   RF64_DS64_MIN_SIZE ||
        (!is_rf64 && !is_bw64) ||
        memcmp(bytes + 8u, "WAVE", 4u) != 0 ||
        memcmp(bytes + RF64_RIFF_HEADER_SIZE, "ds64", 4u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }

    riff_size32 = qw_read_u32_le(bytes + 4u);
    if (is_bw64 && riff_size32 != UINT32_MAX) return QW_STATUS_ERR_FORMAT;
    ds64_size32 = qw_read_u32_le(bytes + RF64_RIFF_HEADER_SIZE + 4u);
    if (ds64_size32 < RF64_DS64_MIN_SIZE ||
        (uint64_t)ds64_size32 > (uint64_t)(size - 20u)) {
        return QW_STATUS_ERR_FORMAT;
    }
    ds64_size = (size_t)ds64_size32;
    riff_size64 = qw_read_u64_le(bytes + 20u);
    data_size64 = qw_read_u64_le(bytes + 28u);
    sample_count64 = is_rf64 ? qw_read_u64_le(bytes + 36u) : 0u;
    table_count32 = qw_read_u32_le(bytes + 44u);
    if ((uint64_t)table_count32 >
        (uint64_t)(ds64_size - RF64_DS64_MIN_SIZE) /
            RF64_DS64_ENTRY_SIZE ||
        RF64_DS64_MIN_SIZE +
                (size_t)table_count32 * RF64_DS64_ENTRY_SIZE != ds64_size) {
        return QW_STATUS_ERR_FORMAT;
    }
    table_count = (size_t)table_count32;
    if (table_count > SIZE_MAX / sizeof(*table)) return QW_STATUS_ERR_FORMAT;
    if (table_count) {
        table = (QwRf64TableEntry *)calloc(table_count, sizeof(*table));
        if (!table) return QW_STATUS_ERR_OUT_OF_MEMORY;
        for (i = 0u; i < table_count; ++i) {
            const uint8_t *entry = bytes + 48u + i * RF64_DS64_ENTRY_SIZE;
            table[i].chunk_id = qw_read_u32_le(entry);
            table[i].chunk_size = qw_read_u64_le(entry + 4u);
        }
    }

    effective_riff_size = riff_size32 == UINT32_MAX
        ? riff_size64 : riff_size32;
    if (effective_riff_size < 4u ||
        effective_riff_size > (uint64_t)(size - 8u) ||
        effective_riff_size > (uint64_t)SIZE_MAX - 8u) {
        goto cleanup;
    }
    riff_end = (size_t)effective_riff_size + 8u;
    ds64_end = 20u + ds64_size;
    if (ds64_end > riff_end || (ds64_size & 1u) > riff_end - ds64_end) {
        goto cleanup;
    }
    offset = ds64_end + (ds64_size & 1u);
    chunks->little_endian = 1;
    chunks->has_metadata = 1;

    while (offset <= riff_end && riff_end - offset >= RF64_CHUNK_HEADER_SIZE) {
        const uint8_t *chunk = bytes + offset;
        uint32_t chunk_id = qw_read_u32_le(chunk);
        uint32_t chunk_size32 = qw_read_u32_le(chunk + 4u);
        uint64_t chunk_size64;
        size_t body_offset = offset + RF64_CHUNK_HEADER_SIZE;
        size_t body_size;
        size_t body_end;
        size_t next;

        rc = rf64_resolve_chunk_size(chunk_id, chunk_size32, data_size64,
                                     &data_size_used, table, table_count,
                                     &chunk_size64);
        if (rc != QW_STATUS_OK) goto cleanup;
        if (chunk_size64 > (uint64_t)(riff_end - body_offset) ||
            chunk_size64 > (uint64_t)SIZE_MAX) {
            rc = QW_STATUS_ERR_FORMAT;
            goto cleanup;
        }
        body_size = (size_t)chunk_size64;
        body_end = body_offset + body_size;
        if ((chunk_size64 & 1u) && body_end == riff_end) {
            rc = QW_STATUS_ERR_FORMAT;
            goto cleanup;
        }
        next = body_end + (size_t)(chunk_size64 & 1u);
        if (next > riff_end) {
            rc = QW_STATUS_ERR_FORMAT;
            goto cleanup;
        }

        if (memcmp(chunk, "fmt ", 4u) == 0) {
            if (format_seen) {
                rc = QW_STATUS_ERR_FORMAT;
                goto cleanup;
            }
            chunks->format = bytes + body_offset;
            chunks->format_size = body_size;
            format_seen = 1;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            if (chunks->data) {
                if (report) {
                    (void)snprintf(report->note, sizeof(report->note),
                                   "native RF64/BW64 parser accepts one data chunk");
                }
                rc = QW_STATUS_ERR_UNSUPPORTED;
                goto cleanup;
            }
            chunks->data = bytes + body_offset;
            chunks->data_size = body_size;
        } else if (memcmp(chunk, "fact", 4u) == 0) {
            if (chunks->fact) {
                chunks->has_duplicate_fact = 1;
            } else {
                uint32_t fact_sample_count32;
                chunks->fact = bytes + body_offset;
                chunks->fact_size = body_size;
                if (body_size >= 4u) {
                    fact_sample_count32 = qw_read_u32_le(bytes + body_offset);
                    if (fact_sample_count32 == UINT32_MAX) {
                        if (is_rf64) {
                            chunks->fact_sample_count = sample_count64;
                            chunks->has_fact_sample_count = 1;
                        } else {
                            chunks->fact_sample_count_unknown = 1;
                        }
                    } else {
                        chunks->fact_sample_count = fact_sample_count32;
                        chunks->has_fact_sample_count = 1;
                    }
                }
            }
            chunks->has_metadata = 1;
        } else {
            chunks->has_metadata = 1;
        }
        offset = next;
    }
    if (offset != riff_end || !format_seen || !chunks->data) {
        rc = QW_STATUS_ERR_FORMAT;
        goto cleanup;
    }
    for (i = 0u; i < table_count; ++i) {
        if (!table[i].used) {
            rc = QW_STATUS_ERR_FORMAT;
            goto cleanup;
        }
    }
    rc = QW_STATUS_OK;

cleanup:
    free(table);
    return rc;
}
