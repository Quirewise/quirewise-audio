#ifndef QUIREWISE_AUDIO_MATROSKA_ADDITIONS_PARSER_H
#define QUIREWISE_AUDIO_MATROSKA_ADDITIONS_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMatroskaBlockAddition {
    const uint8_t *data;
    size_t size;
    uint64_t id;
} QwAudioMatroskaBlockAddition;

enum { QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS = 16u };

typedef struct QwAudioMatroskaBlockAdditions {
    size_t count;
    QwAudioMatroskaBlockAddition items[
        QW_AUDIO_MATROSKA_MAX_BLOCK_ADDITIONS];
} QwAudioMatroskaBlockAdditions;

/* Parses one BlockAdditions payload and exposes its BlockMore values. */
int qw_audio_matroska_additions_parse(
        const uint8_t *data,
        size_t size,
        QwAudioMatroskaBlockAdditions *additions);

#endif
