#ifndef QUIREWISE_AUDIO_H
#define QUIREWISE_AUDIO_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum QwAudioStatus {
    QW_AUDIO_OK = 0,
    QW_AUDIO_INVALID_ARGUMENT = 1,
    QW_AUDIO_UNSUPPORTED = 2,
    QW_AUDIO_FORMAT_ERROR = 3,
    QW_AUDIO_OUT_OF_MEMORY = 4,
    QW_AUDIO_RESOURCE_LIMIT = 5,
    QW_AUDIO_INTERNAL_ERROR = 6,
    QW_AUDIO_END = 7
} QwAudioStatus;

typedef enum QwAudioInputFormat {
    QW_AUDIO_INPUT_WAV = 1,
    QW_AUDIO_INPUT_W64,
    QW_AUDIO_INPUT_AIFF,
    QW_AUDIO_INPUT_CAF,
    QW_AUDIO_INPUT_DSF,
    QW_AUDIO_INPUT_DFF,
    QW_AUDIO_INPUT_DSS,
    QW_AUDIO_INPUT_M4A,
    QW_AUDIO_INPUT_MATROSKA,
    QW_AUDIO_INPUT_WEBM,
    QW_AUDIO_INPUT_MPC,
    QW_AUDIO_INPUT_WMA,
    QW_AUDIO_INPUT_AU,
    QW_AUDIO_INPUT_AAC,
    QW_AUDIO_INPUT_FLAC,
    QW_AUDIO_INPUT_MP2,
    QW_AUDIO_INPUT_MP3,
    QW_AUDIO_INPUT_AMR,
    QW_AUDIO_INPUT_AC3,
    QW_AUDIO_INPUT_DTS,
    QW_AUDIO_INPUT_TTA,
    QW_AUDIO_INPUT_TAK,
    QW_AUDIO_INPUT_WAVPACK,
    QW_AUDIO_INPUT_VOC,
    QW_AUDIO_INPUT_APE,
    QW_AUDIO_INPUT_OGG
} QwAudioInputFormat;

typedef enum QwAudioCodec {
    QW_AUDIO_CODEC_UNKNOWN = 0,
    QW_AUDIO_CODEC_PCM,
    QW_AUDIO_CODEC_ALAW,
    QW_AUDIO_CODEC_MULAW,
    QW_AUDIO_CODEC_ADPCM,
    QW_AUDIO_CODEC_AAC,
    QW_AUDIO_CODEC_FLAC,
    QW_AUDIO_CODEC_MP1,
    QW_AUDIO_CODEC_MP2,
    QW_AUDIO_CODEC_MP3,
    QW_AUDIO_CODEC_AMR_NB,
    QW_AUDIO_CODEC_AMR_WB,
    QW_AUDIO_CODEC_AC3,
    QW_AUDIO_CODEC_MLP,
    QW_AUDIO_CODEC_TRUEHD,
    QW_AUDIO_CODEC_EAC3,
    QW_AUDIO_CODEC_DTS,
    QW_AUDIO_CODEC_TTA,
    QW_AUDIO_CODEC_TAK,
    QW_AUDIO_CODEC_WAVPACK,
    QW_AUDIO_CODEC_APE,
    QW_AUDIO_CODEC_OPUS,
    QW_AUDIO_CODEC_OGG_FLAC,
    QW_AUDIO_CODEC_SPEEX,
    QW_AUDIO_CODEC_VORBIS,
    QW_AUDIO_CODEC_IMA_ADPCM,
    QW_AUDIO_CODEC_MS_ADPCM,
    QW_AUDIO_CODEC_PCM_U8,
    QW_AUDIO_CODEC_PCM_S8,
    QW_AUDIO_CODEC_PCM_S16LE,
    QW_AUDIO_CODEC_PCM_S16BE,
    QW_AUDIO_CODEC_PCM_S24LE,
    QW_AUDIO_CODEC_PCM_S24BE,
    QW_AUDIO_CODEC_PCM_S32LE,
    QW_AUDIO_CODEC_PCM_S32BE,
    QW_AUDIO_CODEC_PCM_F32LE,
    QW_AUDIO_CODEC_PCM_F32BE,
    QW_AUDIO_CODEC_PCM_F64LE,
    QW_AUDIO_CODEC_PCM_F64BE,
    QW_AUDIO_CODEC_PCM_ALAW,
    QW_AUDIO_CODEC_PCM_MULAW,
    QW_AUDIO_CODEC_IMA4,
    QW_AUDIO_CODEC_MACE,
    QW_AUDIO_CODEC_DSD,
    QW_AUDIO_CODEC_DST,
    QW_AUDIO_CODEC_DSS,
    QW_AUDIO_CODEC_MP4_AUDIO,
    QW_AUDIO_CODEC_ALAC,
    QW_AUDIO_CODEC_MATROSKA_AUDIO,
    QW_AUDIO_CODEC_MPC,
    QW_AUDIO_CODEC_WMA,
    QW_AUDIO_CODEC_MPEG_AUDIO,
    QW_AUDIO_CODEC_GSM,
    QW_AUDIO_CODEC_ATRAC1,
    QW_AUDIO_CODEC_AIFF_AUDIO,
    QW_AUDIO_CODEC_QDESIGN,
    QW_AUDIO_CODEC_QDMC,
    QW_AUDIO_CODEC_QDM2,
    QW_AUDIO_CODEC_QCELP,
    QW_AUDIO_CODEC_ILBC
} QwAudioCodec;

typedef struct QwAudioInfo {
    QwAudioInputFormat input_format;
    QwAudioCodec codec;
    uint32_t sample_rate;
    uint16_t channels;
    uint32_t channel_mask;
    uint64_t sample_count;
    uint8_t has_metadata;
} QwAudioInfo;

typedef struct QwAudioBlockAddition {
    const uint8_t *data;
    size_t size;
    uint64_t id;
} QwAudioBlockAddition;

typedef struct QwAudioPacket {
    const uint8_t *data;
    size_t size;
    /* Timestamp and duration use the units supplied by the format parser. */
    uint64_t pts;
    uint32_t duration;
    int64_t discard_padding_ns;
    const QwAudioBlockAddition *block_additions;
    size_t block_addition_count;
    uint8_t decoder_header[8];
    uint8_t decoder_header_size;
    uint8_t no_pts;
} QwAudioPacket;

typedef struct QwAudioReader QwAudioReader;

/*
 * Input bytes are borrowed until qw_audio_reader_close. Track selection is
 * one-based for M4A, Matroska, and WebM; zero selects the default track.
 */
QwAudioStatus qw_audio_reader_open(const uint8_t *data,
                                   size_t size,
                                   QwAudioInputFormat input_format,
                                   size_t requested_track,
                                   QwAudioReader **out_reader);

QwAudioStatus qw_audio_reader_get_info(const QwAudioReader *reader,
                                       QwAudioInfo *out_info);

/* Packet data and block additions are borrowed until the next call or close. */
/* Returns QW_AUDIO_OK for a packet, QW_AUDIO_END at end, or another status. */
QwAudioStatus qw_audio_reader_next_packet(QwAudioReader *reader,
                                          QwAudioPacket *out_packet);

void qw_audio_reader_close(QwAudioReader *reader);

const char *qw_audio_codec_name(QwAudioCodec codec);

#ifdef __cplusplus
}
#endif

#endif
