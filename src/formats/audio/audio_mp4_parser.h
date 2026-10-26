#ifndef QUIREWISE_AUDIO_MP4_PARSER_H
#define QUIREWISE_AUDIO_MP4_PARSER_H

#include "quirewise/convert.h"
#include "audio_mp4_composition_parser.h"
#include "audio_mp4_fragment_parser.h"

#include <stddef.h>
#include <stdint.h>

enum {
    QW_AUDIO_MP4_CONFIG_MAX_SIZE = 64u,
    QW_AUDIO_MP4_ALAC_EXTRADATA_SIZE = 36u,
    QW_AUDIO_MP4_MAX_SAMPLES = 10000000u
};

typedef enum QwAudioMp4Codec {
    QW_AUDIO_MP4_CODEC_AAC = 1,
    QW_AUDIO_MP4_CODEC_ALAC = 2
} QwAudioMp4Codec;

typedef struct QwAudioMp4Source {
    const uint8_t *data;
    size_t size;
    size_t stts_entries_offset;
    size_t stts_entry_count;
    size_t stsc_entries_offset;
    size_t stsc_entry_count;
    size_t sample_size_entries_offset;
    size_t sample_count;
    size_t chunk_entries_offset;
    size_t chunk_count;
    QwAudioMp4FragmentIndex fragment_index;
    QwAudioMp4CompositionTable composition_table;
    uint64_t decoded_sample_count;
    uint64_t presentation_sample_count;
    uint64_t trim_start_samples;
    uint64_t leading_silence_samples;
    uint64_t total_sample_bytes;
    size_t audio_track_count;
    uint32_t sample_description_count;
    int64_t composition_offset;
    uint32_t media_timescale;
    uint32_t fixed_sample_size;
    uint8_t sample_size_field_bits;
    uint32_t frames_per_packet;
    uint32_t max_frame_bytes;
    uint32_t sample_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    uint8_t chunk_offset_size;
    uint8_t has_composition_offsets;
    uint8_t channel_configuration;
    uint8_t audio_object_type;
    uint8_t ps_present;
    uint8_t config[QW_AUDIO_MP4_CONFIG_MAX_SIZE];
    size_t config_size;
    QwAudioMp4Codec codec;
    int has_metadata;
    uint8_t is_fragmented;
} QwAudioMp4Source;

typedef struct QwAudioMp4Cursor {
    size_t sample_index;
    size_t chunk_index;
    size_t stsc_index;
    size_t stts_index;
    size_t chunk_samples_remaining;
    size_t stts_samples_remaining;
    size_t chunk_data_offset;
    uint32_t stts_sample_duration;
    uint64_t next_pts;
    QwAudioMp4CompositionCursor composition_cursor;
} QwAudioMp4Cursor;

typedef struct QwAudioMp4Sample {
    const uint8_t *data;
    size_t size;
    uint64_t pts;
    uint32_t duration;
    uint8_t no_pts;
} QwAudioMp4Sample;

int qw_audio_mp4_parse(const uint8_t *bytes,
                       size_t size,
                       size_t requested_audio_track,
                       QwAudioMp4Source *source,
                       QwConvertReport *report);

void qw_audio_mp4_source_dispose(QwAudioMp4Source *source);

/* Returns 1 with a sample, 0 at end of input, or a PAGE error. */
int qw_audio_mp4_next_sample(const QwAudioMp4Source *source,
                             QwAudioMp4Cursor *cursor,
                             QwAudioMp4Sample *sample);

#endif
