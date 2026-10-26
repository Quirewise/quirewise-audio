#ifndef QUIREWISE_AUDIO_MP4_FRAGMENT_PARSER_H
#define QUIREWISE_AUDIO_MP4_FRAGMENT_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMp4FragmentSample {
    size_t offset;
    uint64_t pts;
    uint32_t size;
    uint32_t duration;
    uint8_t no_pts;
} QwAudioMp4FragmentSample;

typedef struct QwAudioMp4FragmentIndex {
    QwAudioMp4FragmentSample *samples;
    size_t sample_count;
    size_t sample_capacity;
    uint64_t total_sample_bytes;
    uint64_t decoded_duration;
} QwAudioMp4FragmentIndex;

int qw_audio_mp4_parse_fragments(const uint8_t *data,
                                 size_t size,
                                 uint32_t track_id,
                                 uint32_t sample_description_count,
                                 QwAudioMp4FragmentIndex *index);

void qw_audio_mp4_fragment_index_dispose(QwAudioMp4FragmentIndex *index);

#endif
