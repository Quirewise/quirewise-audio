#include "audio_custom_matroska_wavpack.h"

#include "audio_wavpack_matroska_correction_parser.h"
#include "audio_wavpack_matroska_parser.h"
#include "quirewise/status.h"

#include <stdlib.h>

static uint16_t audio_custom_matroska_wavpack_version(
        const QwAudioMatroskaSource *source)
{
    return (uint16_t)source->codec_private[0] |
           (uint16_t)((uint16_t)source->codec_private[1] << 8u);
}

int qw_audio_custom_matroska_wavpack_validate(
        const QwAudioMatroskaSource *source)
{
    QwAudioMatroskaSource cursor;
    QwAudioWavpackMatroskaPacket parsed_packet;
    const uint8_t *packet_data;
    const QwAudioMatroskaBlockAdditions *block_additions;
    size_t packet_size;
    const QwAudioMatroskaBlockAddition *correction_addition;
    size_t addition_index;
    uint16_t version;
    int found_packet = 0;
    int rc;

    if (!source || source->codec != QW_AUDIO_MATROSKA_CODEC_WAVPACK ||
        !source->sample_rate || !source->channels ||
        !source->codec_private || source->codec_private_size != 2u) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    version = audio_custom_matroska_wavpack_version(source);
    cursor = *source;
    for (;;) {
        rc = qw_audio_matroska_next_packet(
            &cursor, &packet_data, &packet_size, NULL, &block_additions);
        if (rc <= 0) break;
        rc = qw_audio_wavpack_matroska_packet_parse(
            packet_data, packet_size, version, source->sample_rate,
            source->channels, &parsed_packet);
        if (rc != QW_STATUS_OK) return rc;
        if (parsed_packet.is_hybrid) {
            if (!source->has_opaque_block_addition_mapping)
                return QW_STATUS_ERR_UNSUPPORTED;
            correction_addition = NULL;
            for (addition_index = 0u;
                 block_additions && addition_index < block_additions->count;
                 ++addition_index) {
                const QwAudioMatroskaBlockAddition *addition =
                    &block_additions->items[addition_index];
                if (addition->id != 1u) continue;
                if (correction_addition) return QW_STATUS_ERR_UNSUPPORTED;
                correction_addition = addition;
            }
            if (!correction_addition)
                return QW_STATUS_ERR_UNSUPPORTED;
            rc = qw_audio_wavpack_matroska_correction_validate(
                correction_addition->data, correction_addition->size,
                source->channels, parsed_packet.block_count);
            if (rc != QW_STATUS_OK) return rc;
        } else {
            for (addition_index = 0u;
                 block_additions && addition_index < block_additions->count;
                 ++addition_index) {
                if (block_additions->items[addition_index].id == 1u)
                    return QW_STATUS_ERR_UNSUPPORTED;
            }
        }
        found_packet = 1;
    }
    if (rc < 0) return rc;
    return found_packet ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_custom_matroska_wavpack_reconstruct_packet(
        const QwAudioMatroskaSource *source,
        const uint8_t *packet_data,
        size_t packet_size,
        uint8_t **buffer,
        size_t *buffer_capacity,
        const uint8_t **reconstructed_data,
        size_t *reconstructed_size)
{
    size_t required_size;
    uint16_t version;
    int rc;

    if (!source || !packet_data || !buffer || !buffer_capacity ||
        !reconstructed_data || !reconstructed_size ||
        source->codec != QW_AUDIO_MATROSKA_CODEC_WAVPACK ||
        !source->codec_private || source->codec_private_size != 2u ||
        !source->sample_rate || !source->channels) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *reconstructed_data = NULL;
    *reconstructed_size = 0u;
    version = audio_custom_matroska_wavpack_version(source);
    rc = qw_audio_wavpack_matroska_packet_reconstruct(
        packet_data, packet_size, version, source->sample_rate,
        source->channels, NULL, 0u, &required_size);
    if (rc != QW_STATUS_OK) return rc;
    if (required_size > *buffer_capacity) {
        uint8_t *grown = (uint8_t *)realloc(*buffer, required_size);
        if (!grown) return QW_STATUS_ERR_OUT_OF_MEMORY;
        *buffer = grown;
        *buffer_capacity = required_size;
    }
    rc = qw_audio_wavpack_matroska_packet_reconstruct(
        packet_data, packet_size, version, source->sample_rate,
        source->channels, *buffer, *buffer_capacity, &required_size);
    if (rc != QW_STATUS_OK) return rc;
    *reconstructed_data = *buffer;
    *reconstructed_size = required_size;
    return QW_STATUS_OK;
}
