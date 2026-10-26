#include "audio_custom_matroska_adpcm_validator.h"

#include "audio_wave_ms_block_validator.h"
#include "audio_wave_ima_format.h"
#include "quirewise/status.h"

static int audio_custom_matroska_validate_adpcm_packets(
        const QwAudioMatroskaSource *source)
{
    QwAudioMatroskaSource cursor;
    const uint8_t *packet_data;
    size_t packet_size;
    int found_packet = 0;
    int rc;

    if (!source ||
        (source->codec != QW_AUDIO_MATROSKA_CODEC_ADPCM_MS &&
         source->codec != QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV) ||
        !source->block_align || !source->channels) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    cursor = *source;
    for (;;) {
        rc = qw_audio_matroska_next_packet(
            &cursor, &packet_data, &packet_size, NULL, NULL);
        if (rc <= 0) break;
        if (source->codec == QW_AUDIO_MATROSKA_CODEC_ADPCM_MS) {
            rc = qw_audio_wave_ms_validate_blocks(packet_data, packet_size,
                                                  source->block_align,
                                                  source->channels,
                                                  source->wave_ms_format
                                                      .coefficient_count);
        } else {
            rc = qw_audio_wave_ima_validate_blocks(packet_data, packet_size,
                                                   source->block_align,
                                                   source->channels);
        }
        if (rc != QW_STATUS_OK) return rc;
        found_packet = 1;
    }
    if (rc < 0) return rc;
    return found_packet ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_custom_matroska_adpcm_validate(
        const QwAudioMatroskaSource *source)
{
    return audio_custom_matroska_validate_adpcm_packets(source);
}
