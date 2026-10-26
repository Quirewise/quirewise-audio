#ifndef QUIREWISE_AUDIO_MATROSKA_PARSER_H
#define QUIREWISE_AUDIO_MATROSKA_PARSER_H

#include "audio_matroska_additions_parser.h"
#include "audio_wave_ms_format.h"

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioMatroskaCodec {
    QW_AUDIO_MATROSKA_CODEC_NONE = 0,
    QW_AUDIO_MATROSKA_CODEC_AAC,
    QW_AUDIO_MATROSKA_CODEC_OPUS,
    QW_AUDIO_MATROSKA_CODEC_VORBIS,
    QW_AUDIO_MATROSKA_CODEC_FLAC,
    QW_AUDIO_MATROSKA_CODEC_ALAC,
    QW_AUDIO_MATROSKA_CODEC_MP2,
    QW_AUDIO_MATROSKA_CODEC_MP3,
    QW_AUDIO_MATROSKA_CODEC_MP1,
    QW_AUDIO_MATROSKA_CODEC_AC3,
    QW_AUDIO_MATROSKA_CODEC_MLP,
    QW_AUDIO_MATROSKA_CODEC_TRUEHD,
    QW_AUDIO_MATROSKA_CODEC_EAC3,
    QW_AUDIO_MATROSKA_CODEC_DTS,
    QW_AUDIO_MATROSKA_CODEC_TTA,
    QW_AUDIO_MATROSKA_CODEC_PCM_U8,
    QW_AUDIO_MATROSKA_CODEC_PCM_S8,
    QW_AUDIO_MATROSKA_CODEC_PCM_S16LE,
    QW_AUDIO_MATROSKA_CODEC_PCM_S16BE,
    QW_AUDIO_MATROSKA_CODEC_PCM_S24LE,
    QW_AUDIO_MATROSKA_CODEC_PCM_S24BE,
    QW_AUDIO_MATROSKA_CODEC_PCM_S32LE,
    QW_AUDIO_MATROSKA_CODEC_PCM_S32BE,
    QW_AUDIO_MATROSKA_CODEC_PCM_F32LE,
    QW_AUDIO_MATROSKA_CODEC_PCM_F32BE,
    QW_AUDIO_MATROSKA_CODEC_PCM_F64LE,
    QW_AUDIO_MATROSKA_CODEC_PCM_F64BE,
    QW_AUDIO_MATROSKA_CODEC_PCM_ALAW,
    QW_AUDIO_MATROSKA_CODEC_PCM_MULAW,
    QW_AUDIO_MATROSKA_CODEC_ATRAC1,
    QW_AUDIO_MATROSKA_CODEC_QDMC,
    QW_AUDIO_MATROSKA_CODEC_QDM2,
    QW_AUDIO_MATROSKA_CODEC_ADPCM_MS,
    QW_AUDIO_MATROSKA_CODEC_ADPCM_IMA_WAV,
    QW_AUDIO_MATROSKA_CODEC_WAVPACK
} QwAudioMatroskaCodec;

typedef struct QwAudioMatroskaSource {
    const uint8_t *data;
    size_t size;
    size_t segment_end;
    size_t next_cluster_position;
    size_t cluster_payload_position;
    size_t cluster_end;
    size_t cluster_child_position;
    uint64_t timecode_scale_ns;
    uint64_t cluster_timecode;
    uint64_t duration_us;
    uint64_t codec_delay_ns;
    uint64_t sample_count;
    uint64_t track_number;
    uint32_t channel_mask;
    uint16_t block_align;
    QwAudioWaveMsFormat wave_ms_format;
    int selected_track_is_default;
    int has_opaque_block_addition_mapping;
    int has_block_additions;
    int is_mpeg1_acm_format;
    QwAudioMatroskaBlockAdditions block_additions;
    int is_webm;
    QwAudioMatroskaCodec codec;
    uint32_t sample_rate;
    uint32_t coded_sample_rate;
    uint32_t mpeg1_bitrate;
    uint16_t channels;
    uint16_t bits_per_sample;
    const uint8_t *codec_private;
    size_t codec_private_size;
    uint8_t generated_codec_private[10];
    uint8_t flac_streaminfo[34];
    size_t audio_track_count;
    const uint8_t *block_frames;
    size_t block_frames_size;
    size_t block_frame_offsets[256];
    size_t block_frame_sizes[256];
    uint16_t block_frame_count;
    uint16_t block_frame_index;
    int64_t block_discard_padding_ns;
    int cluster_loaded;
} QwAudioMatroskaSource;

int qw_audio_matroska_parse(const uint8_t *data,
                            size_t size,
                            size_t requested_audio_track,
                            QwAudioMatroskaSource *source);

int qw_audio_matroska_next_packet(QwAudioMatroskaSource *source,
                                  const uint8_t **packet_data,
                                  size_t *packet_size,
                                  int64_t *discard_padding_ns,
                                  const QwAudioMatroskaBlockAdditions **block_additions);

#endif
