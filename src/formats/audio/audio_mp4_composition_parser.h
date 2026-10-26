#ifndef QUIREWISE_AUDIO_MP4_COMPOSITION_PARSER_H
#define QUIREWISE_AUDIO_MP4_COMPOSITION_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMp4CompositionTable {
    const uint8_t *entries;
    size_t entry_count;
    uint8_t version;
} QwAudioMp4CompositionTable;

typedef struct QwAudioMp4CompositionCursor {
    size_t entry_index;
    uint32_t samples_remaining;
    int64_t current_offset;
} QwAudioMp4CompositionCursor;

int qw_audio_mp4_parse_composition_offsets(const uint8_t *payload,
                                           size_t payload_size,
                                           size_t sample_count,
                                           QwAudioMp4CompositionTable *table);

int qw_audio_mp4_composition_offsets_constant(
    const QwAudioMp4CompositionTable *table,
    int64_t *out_constant_offset);

int qw_audio_mp4_composition_next_offset(
    const QwAudioMp4CompositionTable *table,
    QwAudioMp4CompositionCursor *cursor,
    int64_t *out_offset);

#endif
