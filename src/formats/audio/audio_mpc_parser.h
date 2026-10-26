#ifndef QUIREWISE_AUDIO_MPC_PARSER_H
#define QUIREWISE_AUDIO_MPC_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMpcSource {
    const uint8_t *data;
    size_t size;
    size_t frame_data_end;
    size_t packet_position;
    size_t stream_end_position;
    size_t frame_position;
    uint64_t sample_count;
    uint64_t beginning_silence_samples;
    uint64_t audio_packet_bytes;
    uint32_t frame_count;
    uint32_t current_frame;
    uint32_t frame_samples;
    uint32_t sample_rate;
    uint16_t channels;
    uint8_t frame_bit_position;
    uint8_t decoder_extradata[16];
    uint8_t decoder_extradata_size;
    uint8_t is_sv8;
} QwAudioMpcSource;

typedef struct QwAudioMpcPacket {
    const uint8_t *data;
    size_t size;
    uint8_t decoder_header[4];
    uint8_t decoder_header_size;
} QwAudioMpcPacket;

int qw_audio_mpc_parse(const uint8_t *data,
                       size_t size,
                       QwAudioMpcSource *source);

/* Returns 1 with an audio packet, 0 at stream end, or a PAGE error. */
int qw_audio_mpc_next_packet(QwAudioMpcSource *source,
                             QwAudioMpcPacket *packet);

#endif
