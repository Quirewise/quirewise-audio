#ifndef QUIREWISE_AUDIO_WAVPACK_MATROSKA_PARSER_H
#define QUIREWISE_AUDIO_WAVPACK_MATROSKA_PARSER_H

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_WAVPACK_MATROSKA_MAX_BLOCKS = 8u
};

typedef struct QwAudioWavpackMatroskaBlock {
    const uint8_t *compressed_data;
    size_t compressed_data_size;
    uint32_t flags;
    uint32_t crc;
} QwAudioWavpackMatroskaBlock;

typedef struct QwAudioWavpackMatroskaPacket {
    uint32_t block_samples;
    size_t block_count;
    int is_hybrid;
    int is_dsd;
    QwAudioWavpackMatroskaBlock blocks[
        QW_AUDIO_WAVPACK_MATROSKA_MAX_BLOCKS];
} QwAudioWavpackMatroskaPacket;

/* Rebuilds WavPack blocks stored in Matroska's compact form.
 * With output == NULL, validates the packet and returns the required size. */
int qw_audio_wavpack_matroska_packet_reconstruct(
        const uint8_t *bytes,
        size_t size,
        uint16_t version,
        uint32_t sample_rate,
        uint16_t channels,
        uint8_t *output,
        size_t output_capacity,
        size_t *output_size);
int qw_audio_wavpack_matroska_packet_parse(
        const uint8_t *bytes,
        size_t size,
        uint16_t version,
        uint32_t sample_rate,
        uint16_t channels,
        QwAudioWavpackMatroskaPacket *packet);

#endif
