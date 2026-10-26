#include "audio_aiff_g711_parser.h"

#include "audio_aiff_chunks.h"
#include "quirewise/status.h"

#include <stdio.h>
#include <string.h>

int qw_audio_aiff_g711_parse(const uint8_t *bytes,
                             size_t size,
                             QwAudioAiffG711Source *source,
                             QwConvertReport *report)
{
    QwAudioAiffChunks chunks = {0};
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_aiff_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) return rc;
    if (!chunks.aifc ||
        (memcmp(chunks.compression_type, "alaw", 4u) != 0 &&
         memcmp(chunks.compression_type, "ALAW", 4u) != 0 &&
         memcmp(chunks.compression_type, "ulaw", 4u) != 0 &&
         memcmp(chunks.compression_type, "ULAW", 4u) != 0)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (chunks.channels == 0u || chunks.channels > 8u ||
        chunks.sample_rate < 4000u || chunks.sample_rate > 384000u ||
        chunks.sample_bits != 8u) {
        if (report) {
            (void)snprintf(report->note, sizeof(report->note),
                           "native AIFC G.711 parser supports 8-bit mono through 8-channel A-law or mu-law streams");
        }
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (chunks.frame_count > SIZE_MAX / chunks.channels ||
        chunks.sample_bytes != (size_t)chunks.frame_count * chunks.channels) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!chunks.frame_count) return QW_STATUS_ERR_FORMAT;
    source->data = chunks.sample_data;
    source->size = chunks.sample_bytes;
    source->sample_count = chunks.frame_count;
    source->sample_rate = chunks.sample_rate;
    source->channels = chunks.channels;
    source->codec = memcmp(chunks.compression_type, "alaw", 4u) == 0 ||
                    memcmp(chunks.compression_type, "ALAW", 4u) == 0
        ? QW_AUDIO_AIFF_G711_ALAW : QW_AUDIO_AIFF_G711_MULAW;
    source->has_metadata = chunks.has_metadata;
    return QW_STATUS_OK;
}
