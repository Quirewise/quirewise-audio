#include "audio_wave64_parser.h"

#include "audio_wave_chunks.h"
#include "audio_pcm_parser.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_wave64_parse_pcm(const uint8_t *bytes,
                              size_t size,
                              QwAudioPcmSource *source,
                              QwConvertReport *report)
{
    QwAudioWaveChunks chunks = {0};
    int rc;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_wave64_parse_chunks(bytes, size, &chunks, report);
    if (rc != QW_STATUS_OK) {
        memset(source, 0, sizeof(*source));
        return rc;
    }
    return qw_audio_pcm_parse_wave_chunks(&chunks, source, report);
}
