#ifndef QUIREWISE_AUDIO_AIFF_CODEC_PARSER_H
#define QUIREWISE_AUDIO_AIFF_CODEC_PARSER_H

#include "audio_aiff_chunks.h"

typedef enum QwAudioAiffCodec {
    QW_AUDIO_AIFF_CODEC_GSM = 1,
    QW_AUDIO_AIFF_CODEC_G722,
    QW_AUDIO_AIFF_CODEC_G726,
    QW_AUDIO_AIFF_CODEC_G728,
    QW_AUDIO_AIFF_CODEC_QDMC,
    QW_AUDIO_AIFF_CODEC_QDM2,
    QW_AUDIO_AIFF_CODEC_QCELP,
    QW_AUDIO_AIFF_CODEC_CBD2,
    QW_AUDIO_AIFF_CODEC_SDX2,
    QW_AUDIO_AIFF_CODEC_ADP4,
    QW_AUDIO_AIFF_CODEC_N64,
    QW_AUDIO_AIFF_CODEC_ADPCM_XA
} QwAudioAiffCodec;

typedef struct QwAudioAiffCodecSource {
    /* Borrowed from the source buffer; extradata is copied by the decoder
     * adapter when it opens libavcodec.
     */
    const uint8_t *sample_data;
    size_t sample_bytes;
    const uint8_t *extradata;
    size_t extradata_size;
    size_t block_align;
    uint64_t sample_count;
    uint32_t sample_rate;
    uint32_t block_duration;
    uint16_t channels;
    uint16_t sample_bits;
    uint16_t coded_bits;
    QwAudioAiffCodec codec;
    int one_block_per_packet;
    int has_metadata;
} QwAudioAiffCodecSource;

typedef struct QwAudioAiffCodecPacket {
    const uint8_t *data;
    size_t size;
    size_t next_position;
} QwAudioAiffCodecPacket;

int qw_audio_aiff_codec_parse(const uint8_t *bytes,
                              size_t size,
                              QwAudioAiffCodecSource *source,
                              QwConvertReport *report);
int qw_audio_aiff_codec_next_packet(const QwAudioAiffCodecSource *source,
                                    size_t position,
                                    QwAudioAiffCodecPacket *packet);

#endif
