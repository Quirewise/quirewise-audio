#ifndef QUIREWISE_AUDIO_VOC_BLOCKS_H
#define QUIREWISE_AUDIO_VOC_BLOCKS_H

#include <stddef.h>
#include <stdint.h>

typedef enum QwAudioVocBlockType {
    QW_AUDIO_VOC_BLOCK_TERMINATOR = 0,
    QW_AUDIO_VOC_BLOCK_SOUND_DATA = 1,
    QW_AUDIO_VOC_BLOCK_SOUND_CONTINUATION = 2,
    QW_AUDIO_VOC_BLOCK_SILENCE = 3,
    QW_AUDIO_VOC_BLOCK_MARKER = 4,
    QW_AUDIO_VOC_BLOCK_TEXT = 5,
    QW_AUDIO_VOC_BLOCK_REPEAT_START = 6,
    QW_AUDIO_VOC_BLOCK_REPEAT_END = 7,
    QW_AUDIO_VOC_BLOCK_EXTRA_INFO = 8,
    QW_AUDIO_VOC_BLOCK_NEW_DATA = 9
} QwAudioVocBlockType;

typedef struct QwAudioVocBlock {
    uint8_t type;
    const uint8_t *payload;
    size_t payload_size;
    size_t offset;
    size_t next_offset;
} QwAudioVocBlock;

int qw_audio_voc_container_parse_header(const uint8_t *bytes,
                                        size_t size,
                                        size_t *data_offset);
int qw_audio_voc_block_next(const uint8_t *bytes,
                            size_t size,
                            size_t position,
                            size_t end,
                            QwAudioVocBlock *block);

#endif
