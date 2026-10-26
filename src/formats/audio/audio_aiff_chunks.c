#include "audio_aiff_chunks.h"
#include "audio_aiff_extended80.h"
#include "audio_aiff_apcm_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

static int aiff_parse_appl_vadpcm_codes(const uint8_t *body,
                                        size_t body_size,
                                        QwAudioAiffChunks *chunks)
{
    static const uint8_t stoc_tag[4] = {'s', 't', 'o', 'c'};
    static const uint8_t vadpcm_codes_tag[11] = {
        'V', 'A', 'D', 'P', 'C', 'M', 'C', 'O', 'D', 'E', 'S'
    };
    if (!body || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (body_size < 16u || memcmp(body, stoc_tag, sizeof(stoc_tag)) != 0 ||
        body[4] != sizeof(vadpcm_codes_tag) ||
        memcmp(body + 5u, vadpcm_codes_tag, sizeof(vadpcm_codes_tag)) != 0 ||
        body_size == 16u) {
        return QW_STATUS_OK;
    }
    if (chunks->has_codec_extradata) return QW_STATUS_ERR_FORMAT;
    chunks->codec_extradata = body + 16u;
    chunks->codec_extradata_size = body_size - 16u;
    chunks->has_codec_extradata = 1;
    return QW_STATUS_OK;
}

int qw_audio_aiff_parse_chunks(const uint8_t *bytes,
                               size_t size,
                               QwAudioAiffChunks *chunks,
                               QwConvertReport *report)
{
    size_t form_end;
    size_t offset;
    int comm_seen = 0;

    if (!bytes || !chunks) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(chunks, 0, sizeof(*chunks));
    if (size < 12u || memcmp(bytes, "FORM", 4u) != 0 ||
        (memcmp(bytes + 8u, "AIFF", 4u) != 0 &&
         memcmp(bytes + 8u, "AIFC", 4u) != 0)) {
        return QW_STATUS_ERR_FORMAT;
    }
    chunks->aifc = memcmp(bytes + 8u, "AIFC", 4u) == 0;
    if (!chunks->aifc) memcpy(chunks->compression_type, "NONE", 4u);
    {
        uint32_t form_size = qw_read_u32_be(bytes + 4u);
        if (form_size < 4u || (uint64_t)form_size + 8u > size)
            return QW_STATUS_ERR_FORMAT;
        form_end = (size_t)form_size + 8u;
    }
    if (form_end < size) chunks->has_metadata = 1;

    offset = 12u;
    while (offset <= form_end && form_end - offset >= 8u) {
        const uint8_t *chunk = bytes + offset;
        uint32_t chunk_size = qw_read_u32_be(chunk + 4u);
        size_t body_offset = offset + 8u;
        const uint8_t *body = bytes + body_offset;
        size_t body_size = (size_t)chunk_size;
        size_t body_end;
        size_t next;

        if (body_size > form_end - body_offset) return QW_STATUS_ERR_FORMAT;
        body_end = body_offset + body_size;
        if (memcmp(chunk, "COMM", 4u) == 0) {
            if (comm_seen || chunk_size < 18u) return QW_STATUS_ERR_FORMAT;
            chunks->channels = qw_read_u16_be(body);
            chunks->frame_count = qw_read_u32_be(body + 2u);
            chunks->sample_bits = qw_read_u16_be(body + 6u);
            if (!qw_audio_aiff_extended80_to_rate(body + 8u,
                                                   &chunks->sample_rate))
                return QW_STATUS_ERR_FORMAT;
            if (chunks->aifc) {
                if (chunk_size < 22u) return QW_STATUS_ERR_FORMAT;
                memcpy(chunks->compression_type, body + 18u, 4u);
            }
            comm_seen = 1;
        } else if (memcmp(chunk, "SSND", 4u) == 0) {
            uint32_t sound_offset;
            if (chunks->sample_data || chunks->has_apcm || chunk_size < 8u)
                return QW_STATUS_ERR_FORMAT;
            sound_offset = qw_read_u32_be(body);
            if (sound_offset > chunk_size - 8u) return QW_STATUS_ERR_FORMAT;
            chunks->sample_data = body + 8u + sound_offset;
            chunks->sample_bytes = (size_t)chunk_size - 8u - sound_offset;
        } else if (memcmp(chunk, "APCM", 4u) == 0) {
            QwAudioAiffApcmChunk apcm = {0};
            int rc;
            if (chunks->sample_data || chunks->has_apcm)
                return QW_STATUS_ERR_FORMAT;
            rc = qw_audio_aiff_apcm_parse_chunk(body, body_size, &apcm);
            if (rc != QW_STATUS_OK) return rc;
            chunks->sample_data = apcm.sample_data;
            chunks->sample_bytes = apcm.sample_bytes;
            chunks->apcm_block_align = apcm.block_align;
            chunks->has_apcm = 1;
        } else if (memcmp(chunk, "wave", 4u) == 0) {
            if (chunks->has_codec_extradata) return QW_STATUS_ERR_FORMAT;
            chunks->codec_extradata = body;
            chunks->codec_extradata_size = body_size;
            chunks->has_codec_extradata = 1;
            chunks->has_metadata = 1;
        } else if (memcmp(chunk, "APPL", 4u) == 0) {
            int rc = aiff_parse_appl_vadpcm_codes(body, body_size, chunks);
            if (rc != QW_STATUS_OK) return rc;
            chunks->has_metadata = 1;
        } else {
            chunks->has_metadata = 1;
        }
        if ((chunk_size & 1u) && body_end >= form_end)
            return QW_STATUS_ERR_FORMAT;
        next = body_end + (chunk_size & 1u);
        if (next > form_end) return QW_STATUS_ERR_FORMAT;
        offset = next;
    }
    if (offset != form_end || !comm_seen || !chunks->sample_data) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (report && !chunks->frame_count) {
        (void)snprintf(report->note, sizeof(report->note),
                       "AIFF COMM chunk declares no sample frames");
    }
    return QW_STATUS_OK;
}
