#include "audio_aiff_apcm_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_aiff_apcm_parse_chunk(const uint8_t *body,
                                   size_t body_size,
                                   QwAudioAiffApcmChunk *chunk)
{
    if (!body || !chunk) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(chunk, 0, sizeof(*chunk));
    if (body_size < 8u) return QW_STATUS_ERR_FORMAT;

    chunk->block_align = qw_read_u32_be(body + 4u);
    if (!chunk->block_align) return QW_STATUS_ERR_FORMAT;
    chunk->sample_data = body + 8u;
    chunk->sample_bytes = body_size - 8u;
    return QW_STATUS_OK;
}
