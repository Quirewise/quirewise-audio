#ifndef QUIREWISE_AUDIO_VOC_REPEAT_PARSER_H
#define QUIREWISE_AUDIO_VOC_REPEAT_PARSER_H

#include "audio_voc_blocks.h"

#include <stddef.h>
#include <stdint.h>

enum { QW_AUDIO_VOC_MAX_REPEAT_DEPTH = 16u };

typedef struct QwAudioVocLoopCursor {
    size_t start_position;
    size_t end_position;
    uint32_t repeats_remaining;
} QwAudioVocLoopCursor;

typedef struct QwAudioVocCursor {
    size_t position;
    size_t loop_depth;
    QwAudioVocLoopCursor loops[QW_AUDIO_VOC_MAX_REPEAT_DEPTH];
} QwAudioVocCursor;

int qw_audio_voc_repeat_read_count(const QwAudioVocBlock *block,
                                   uint32_t *additional_repeats);
int qw_audio_voc_repeat_validate_end(const QwAudioVocBlock *block);
int qw_audio_voc_repeat_find_matching_end(const uint8_t *data,
                                          size_t size,
                                          size_t start,
                                          size_t end,
                                          size_t *matching_end);
int qw_audio_voc_repeat_add_samples(uint64_t *sample_count,
                                    uint64_t segment_samples,
                                    uint32_t additional_repeats);
int qw_audio_voc_repeat_cursor_start(const uint8_t *data,
                                     size_t size,
                                     size_t frame_data_end,
                                     const QwAudioVocBlock *block,
                                     QwAudioVocCursor *cursor);
int qw_audio_voc_repeat_cursor_end(const QwAudioVocBlock *block,
                                   QwAudioVocCursor *cursor);

#endif
