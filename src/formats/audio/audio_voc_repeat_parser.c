#include "audio_voc_repeat_parser.h"

#include "core/byte_utils.h"
#include "quirewise/convert.h"

#include <string.h>

int qw_audio_voc_repeat_read_count(const QwAudioVocBlock *block,
                                   uint32_t *additional_repeats)
{
    if (!block || !additional_repeats ||
        block->type != QW_AUDIO_VOC_BLOCK_REPEAT_START) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    if (block->payload_size != 2u || !block->payload) return QW_STATUS_ERR_FORMAT;
    *additional_repeats = qw_read_u16_le(block->payload);
    if (*additional_repeats == UINT16_MAX) return QW_STATUS_ERR_UNSUPPORTED;
    return QW_STATUS_OK;
}

int qw_audio_voc_repeat_validate_end(const QwAudioVocBlock *block)
{
    if (!block || block->type != QW_AUDIO_VOC_BLOCK_REPEAT_END)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    return block->payload_size == 0u ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

int qw_audio_voc_repeat_find_matching_end(const uint8_t *data,
                                          size_t size,
                                          size_t start,
                                          size_t end,
                                          size_t *matching_end)
{
    size_t position = start;
    size_t nested_depth = 0u;
    if (!data || !matching_end || start > end || end > size)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    while (position < end) {
        QwAudioVocBlock block;
        int rc = qw_audio_voc_block_next(data, size, position, end, &block);
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (block.type == QW_AUDIO_VOC_BLOCK_REPEAT_START) {
            uint32_t ignored_repeats;
            rc = qw_audio_voc_repeat_read_count(&block, &ignored_repeats);
            if (rc != QW_STATUS_OK) return rc;
            if (nested_depth >= QW_AUDIO_VOC_MAX_REPEAT_DEPTH)
                return QW_STATUS_ERR_UNSUPPORTED;
            ++nested_depth;
        } else if (block.type == QW_AUDIO_VOC_BLOCK_REPEAT_END) {
            rc = qw_audio_voc_repeat_validate_end(&block);
            if (rc != QW_STATUS_OK) return rc;
            if (!nested_depth) {
                *matching_end = block.offset;
                return QW_STATUS_OK;
            }
            --nested_depth;
        } else if (block.type == QW_AUDIO_VOC_BLOCK_TERMINATOR) {
            return QW_STATUS_ERR_FORMAT;
        }
        position = block.next_offset;
    }
    return QW_STATUS_ERR_FORMAT;
}

int qw_audio_voc_repeat_add_samples(uint64_t *sample_count,
                                    uint64_t segment_samples,
                                    uint32_t additional_repeats)
{
    uint64_t extra_samples;
    if (!sample_count) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (!segment_samples || !additional_repeats) return QW_STATUS_OK;
    if (segment_samples > UINT64_MAX / additional_repeats)
        return QW_STATUS_ERR_FORMAT;
    extra_samples = segment_samples * additional_repeats;
    if (extra_samples > UINT64_MAX - *sample_count) return QW_STATUS_ERR_FORMAT;
    *sample_count += extra_samples;
    return QW_STATUS_OK;
}

int qw_audio_voc_repeat_cursor_start(const uint8_t *data,
                                     size_t size,
                                     size_t frame_data_end,
                                     const QwAudioVocBlock *block,
                                     QwAudioVocCursor *cursor)
{
    uint32_t additional_repeats;
    size_t matching_end;
    QwAudioVocLoopCursor *loop;
    int rc;
    if (!cursor) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (cursor->loop_depth >= QW_AUDIO_VOC_MAX_REPEAT_DEPTH)
        return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_voc_repeat_read_count(block, &additional_repeats);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_voc_repeat_find_matching_end(
        data, size, block->next_offset, frame_data_end, &matching_end);
    if (rc != QW_STATUS_OK) return rc;
    loop = &cursor->loops[cursor->loop_depth];
    memset(loop, 0, sizeof(*loop));
    loop->start_position = block->next_offset;
    loop->end_position = matching_end;
    loop->repeats_remaining = additional_repeats;
    ++cursor->loop_depth;
    cursor->position = block->next_offset;
    return QW_STATUS_OK;
}

int qw_audio_voc_repeat_cursor_end(const QwAudioVocBlock *block,
                                   QwAudioVocCursor *cursor)
{
    QwAudioVocLoopCursor *loop;
    int rc;
    if (!cursor) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_voc_repeat_validate_end(block);
    if (rc != QW_STATUS_OK) return rc;
    if (!cursor->loop_depth ||
        cursor->loop_depth > QW_AUDIO_VOC_MAX_REPEAT_DEPTH) {
        return QW_STATUS_ERR_FORMAT;
    }
    loop = &cursor->loops[cursor->loop_depth - 1u];
    if (loop->end_position != block->offset) return QW_STATUS_ERR_FORMAT;
    if (loop->repeats_remaining) {
        --loop->repeats_remaining;
        cursor->position = loop->start_position;
    } else {
        --cursor->loop_depth;
        cursor->position = block->next_offset;
    }
    return QW_STATUS_OK;
}
