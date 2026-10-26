#include "audio_wave_chunks.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

enum {
    WAVE64_HEADER_SIZE = 40u,
    WAVE64_CHUNK_HEADER_SIZE = 24u
};

static const uint8_t wave64_riff_guid[16] = {
    0x72u, 0x69u, 0x66u, 0x66u, 0x2eu, 0x91u, 0xcfu, 0x11u,
    0xa5u, 0xd6u, 0x28u, 0xdbu, 0x04u, 0xc1u, 0x00u, 0x00u
};

static const uint8_t wave64_wave_guid[16] = {
    0x77u, 0x61u, 0x76u, 0x65u, 0xf3u, 0xacu, 0xd3u, 0x11u,
    0x8cu, 0xd1u, 0x00u, 0xc0u, 0x4fu, 0x8eu, 0xdbu, 0x8au
};

static const uint8_t wave64_fmt_guid[16] = {
    0x66u, 0x6du, 0x74u, 0x20u, 0xf3u, 0xacu, 0xd3u, 0x11u,
    0x8cu, 0xd1u, 0x00u, 0xc0u, 0x4fu, 0x8eu, 0xdbu, 0x8au
};

static const uint8_t wave64_data_guid[16] = {
    0x64u, 0x61u, 0x74u, 0x61u, 0xf3u, 0xacu, 0xd3u, 0x11u,
    0x8cu, 0xd1u, 0x00u, 0xc0u, 0x4fu, 0x8eu, 0xdbu, 0x8au
};

static const uint8_t wave64_fact_guid[16] = {
    0x66u, 0x61u, 0x63u, 0x74u, 0xf3u, 0xacu, 0xd3u, 0x11u,
    0x8cu, 0xd1u, 0x00u, 0xc0u, 0x4fu, 0x8eu, 0xdbu, 0x8au
};

int qw_audio_wave64_parse_chunks(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioWaveChunks *chunks,
                                 QwConvertReport *report)
{
    uint64_t declared_file_size;
    size_t file_end;
    size_t offset;

    if (!bytes || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(chunks, 0, sizeof(*chunks));
    if (size < WAVE64_HEADER_SIZE ||
        memcmp(bytes, wave64_riff_guid, sizeof(wave64_riff_guid)) != 0 ||
        memcmp(bytes + 24u, wave64_wave_guid, sizeof(wave64_wave_guid)) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    declared_file_size = qw_read_u64_le(bytes + 16u);
    if (declared_file_size < WAVE64_HEADER_SIZE ||
        declared_file_size > (uint64_t)size ||
        declared_file_size > (uint64_t)SIZE_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    file_end = (size_t)declared_file_size;
    chunks->little_endian = 1;
    if (file_end < size) chunks->has_metadata = 1;

    offset = WAVE64_HEADER_SIZE;
    while (offset < file_end) {
        const uint8_t *chunk;
        uint64_t chunk_size;
        uint64_t aligned_chunk_size;
        size_t payload_size;
        size_t payload_offset;
        size_t chunk_end;
        size_t next;

        if (file_end - offset < WAVE64_CHUNK_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        chunk = bytes + offset;
        chunk_size = qw_read_u64_le(chunk + 16u);
        if (chunk_size < WAVE64_CHUNK_HEADER_SIZE ||
            chunk_size > (uint64_t)(file_end - offset)) {
            return QW_STATUS_ERR_FORMAT;
        }
        payload_size = (size_t)(chunk_size - WAVE64_CHUNK_HEADER_SIZE);
        payload_offset = offset + WAVE64_CHUNK_HEADER_SIZE;
        chunk_end = offset + (size_t)chunk_size;

        if (memcmp(chunk, wave64_fmt_guid, sizeof(wave64_fmt_guid)) == 0) {
            if (chunks->format) return QW_STATUS_ERR_FORMAT;
            chunks->format = bytes + payload_offset;
            chunks->format_size = payload_size;
        } else if (memcmp(chunk, wave64_data_guid,
                          sizeof(wave64_data_guid)) == 0) {
            if (chunks->data) {
                if (report) {
                    (void)snprintf(report->note, sizeof(report->note),
                                   "native Wave64 parser accepts one data chunk");
                }
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            chunks->data = bytes + payload_offset;
            chunks->data_size = payload_size;
        } else if (memcmp(chunk, wave64_fact_guid,
                          sizeof(wave64_fact_guid)) == 0) {
            if (chunks->fact) {
                chunks->has_duplicate_fact = 1;
            } else {
                chunks->fact = bytes + payload_offset;
                chunks->fact_size = payload_size;
                if (payload_size >= 4u) {
                    uint32_t sample_count =
                        qw_read_u32_le(bytes + payload_offset);
                    if (sample_count == UINT32_MAX) {
                        chunks->fact_sample_count_unknown = 1;
                    } else {
                        chunks->fact_sample_count = sample_count;
                        chunks->has_fact_sample_count = 1;
                    }
                }
            }
            chunks->has_metadata = 1;
        } else {
            chunks->has_metadata = 1;
        }

        if (chunk_size > UINT64_MAX - 7u) return QW_STATUS_ERR_FORMAT;
        aligned_chunk_size = (chunk_size + 7u) & ~UINT64_C(7);
        if (aligned_chunk_size > (uint64_t)(file_end - offset))
            return QW_STATUS_ERR_FORMAT;
        next = offset + (size_t)aligned_chunk_size;
        if (chunk_end > next) return QW_STATUS_ERR_FORMAT;
        offset = next;
    }
    if (offset != file_end || !chunks->format || !chunks->data)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
