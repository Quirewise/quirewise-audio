#include "audio_wave_chunks.h"
#include "audio_wave_rf64_bw64_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

int qw_audio_wave_parse_chunks(const uint8_t *bytes,
                               size_t size,
                               QwAudioWaveChunks *chunks,
                               QwConvertReport *report)
{
    size_t riff_end;
    size_t offset;
    uint32_t declared_size;
    int format_seen = 0;

    if (!bytes || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (size >= 4u &&
        (memcmp(bytes, "RF64", 4u) == 0 ||
         memcmp(bytes, "BW64", 4u) == 0)) {
        return qw_audio_wave_rf64_bw64_parse_chunks(bytes, size, chunks,
                                                    report);
    }
    memset(chunks, 0, sizeof(*chunks));
    if (size < 12u ||
        (memcmp(bytes, "RIFF", 4u) != 0 &&
         memcmp(bytes, "RIFX", 4u) != 0) ||
        memcmp(bytes + 8u, "WAVE", 4u) != 0) {
        return QW_STATUS_ERR_FORMAT;
    }
    chunks->little_endian = memcmp(bytes, "RIFF", 4u) == 0;
    declared_size = chunks->little_endian
        ? qw_read_u32_le(bytes + 4u) : qw_read_u32_be(bytes + 4u);
    if (declared_size < 4u || (uint64_t)declared_size + 8u > size)
        return QW_STATUS_ERR_FORMAT;
    riff_end = (size_t)declared_size + 8u;
    if (riff_end < size) chunks->has_metadata = 1;

    offset = 12u;
    while (offset <= riff_end && riff_end - offset >= 8u) {
        const uint8_t *chunk = bytes + offset;
        uint32_t chunk_size = chunks->little_endian
            ? qw_read_u32_le(chunk + 4u) : qw_read_u32_be(chunk + 4u);
        size_t body_offset = offset + 8u;
        size_t body_size = (size_t)chunk_size;
        size_t body_end;
        size_t next;

        if (body_size > riff_end - body_offset) return QW_STATUS_ERR_FORMAT;
        body_end = body_offset + body_size;
        if (memcmp(chunk, "fmt ", 4u) == 0) {
            if (format_seen) return QW_STATUS_ERR_FORMAT;
            chunks->format = bytes + body_offset;
            chunks->format_size = body_size;
            format_seen = 1;
        } else if (memcmp(chunk, "data", 4u) == 0) {
            if (chunks->data) {
                if (report) {
                    (void)snprintf(report->note, sizeof(report->note),
                                   "native WAVE parser accepts one data chunk");
                }
                return QW_STATUS_ERR_UNSUPPORTED;
            }
            chunks->data = bytes + body_offset;
            chunks->data_size = body_size;
        } else if (memcmp(chunk, "fact", 4u) == 0) {
            if (chunks->fact) {
                chunks->has_duplicate_fact = 1;
            } else {
                chunks->fact = bytes + body_offset;
                chunks->fact_size = body_size;
                if (body_size >= 4u) {
                    uint32_t sample_count = chunks->little_endian
                        ? qw_read_u32_le(bytes + body_offset)
                        : qw_read_u32_be(bytes + body_offset);
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
        if ((chunk_size & 1u) && body_end >= riff_end)
            return QW_STATUS_ERR_FORMAT;
        next = body_end + (chunk_size & 1u);
        if (next > riff_end) return QW_STATUS_ERR_FORMAT;
        offset = next;
    }
    if (offset != riff_end || !format_seen || !chunks->data)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}

uint64_t qw_audio_wave_fact_sample_count(const QwAudioWaveChunks *chunks)
{
    if (!chunks || !chunks->fact || chunks->fact_size < 4u) return 0u;
    if (chunks->fact_sample_count_unknown) return 0u;
    if (chunks->has_fact_sample_count) return chunks->fact_sample_count;
    return chunks->little_endian
        ? qw_read_u32_le(chunks->fact) : qw_read_u32_be(chunks->fact);
}
