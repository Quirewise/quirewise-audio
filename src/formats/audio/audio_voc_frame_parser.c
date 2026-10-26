#include "audio_voc_parser_internal.h"

#include "audio_voc_blocks.h"
#include "audio_voc_repeat_parser.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

int qw_audio_voc_next_frame(const QwAudioVocSource *source,
                            QwAudioVocCursor *cursor,
                            QwAudioVocFrame *frame)
{
    size_t position;
    if (!source || !cursor || !frame || !source->data)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (cursor->loop_depth > QW_AUDIO_VOC_MAX_REPEAT_DEPTH)
        return QW_STATUS_ERR_FORMAT;
    if (!cursor->position) cursor->position = source->data_start;
    position = cursor->position;
    if (position == source->frame_data_end)
        return cursor->loop_depth ? QW_STATUS_ERR_FORMAT : 0;
    if (position < source->data_start || position > source->frame_data_end)
        return QW_STATUS_ERR_FORMAT;
    while (position < source->frame_data_end) {
        QwAudioVocBlock block;
        size_t data_offset;
        size_t block_align;
        size_t data_size;
        int has_initial_adpcm_sample = 0;
        int rc = qw_audio_voc_block_next(source->data, source->size,
                                         position, source->frame_data_end,
                                         &block);
        if (rc <= 0) return rc < 0 ? rc : 0;
        if (block.type == QW_AUDIO_VOC_BLOCK_REPEAT_START) {
            rc = qw_audio_voc_repeat_cursor_start(
                source->data, source->size, source->frame_data_end,
                &block, cursor);
            if (rc != QW_STATUS_OK) return rc;
            position = cursor->position;
            continue;
        }
        if (block.type == QW_AUDIO_VOC_BLOCK_REPEAT_END) {
            rc = qw_audio_voc_repeat_cursor_end(&block, cursor);
            if (rc != QW_STATUS_OK) return rc;
            position = cursor->position;
            continue;
        }
        switch (block.type) {
        case QW_AUDIO_VOC_BLOCK_NEW_DATA: {
            uint32_t sample_rate;
            uint16_t bits_per_sample;
            uint16_t channels;
            uint16_t codec_tag;
            if (block.payload_size < AUDIO_VOC_NEW_DATA_HEADER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            sample_rate = qw_read_u32_le(block.payload);
            bits_per_sample = block.payload[4u];
            channels = block.payload[5u];
            codec_tag = qw_read_u16_le(block.payload + 6u);
            if (sample_rate != source->sample_rate ||
                channels != source->channels ||
                bits_per_sample != source->bits_per_sample ||
                codec_tag != (uint16_t)source->codec) {
                return QW_STATUS_ERR_FORMAT;
            }
            if (qw_audio_voc_codec_is_adpcm(source->codec)) {
                QwAudioVocAdpcmMode mode;
                if (!qw_audio_voc_codec_adpcm_mode(source->codec,
                                                   bits_per_sample, &mode) ||
                    !qw_audio_voc_adpcm_mode_supports_channels(mode,
                                                              channels)) {
                    return QW_STATUS_ERR_FORMAT;
                }
                block_align = 0u;
                has_initial_adpcm_sample =
                    qw_audio_voc_codec_has_adpcm_initial_sample(
                        source->codec);
            } else {
                block_align = (size_t)channels * (bits_per_sample / 8u);
            }
            data_offset = AUDIO_VOC_NEW_DATA_HEADER_SIZE;
            break;
        }
        case QW_AUDIO_VOC_BLOCK_SOUND_DATA: {
            uint32_t sample_rate;
            uint8_t packing_method;
            if (block.payload_size < AUDIO_VOC_LEGACY_DATA_HEADER_SIZE)
                return QW_STATUS_ERR_FORMAT;
            sample_rate = 1000000u /
                (256u - (uint32_t)block.payload[0u]);
            packing_method = block.payload[1u];
            if (position >= source->data_start + 8u) {
                QwAudioVocBlock extra_info;
                int extra_rc = qw_audio_voc_block_next(
                    source->data, source->size, position - 8u, position,
                    &extra_info);
                if (extra_rc < 0) return extra_rc;
                if (extra_rc > 0 &&
                    extra_info.type == QW_AUDIO_VOC_BLOCK_EXTRA_INFO &&
                    extra_info.next_offset == position) {
                    uint16_t time_constant;
                    uint16_t extra_channels;
                    uint32_t denominator;
                    if (extra_info.payload_size != 4u ||
                        extra_info.payload[2u] > 3u ||
                        extra_info.payload[3u] > 1u) {
                        return QW_STATUS_ERR_FORMAT;
                    }
                    time_constant = qw_read_u16_le(extra_info.payload);
                    extra_channels =
                        (uint16_t)extra_info.payload[3u] + 1u;
                    packing_method = extra_info.payload[2u];
                    denominator = 65536u - (uint32_t)time_constant;
                    sample_rate = 256000000u /
                        ((uint32_t)extra_channels * denominator);
                    if (extra_channels != source->channels)
                        return QW_STATUS_ERR_FORMAT;
                }
            }
            if (sample_rate != source->sample_rate) return QW_STATUS_ERR_FORMAT;
            if (qw_audio_voc_codec_is_adpcm(source->codec)) {
                QwAudioVocAdpcmMode packing_mode;
                QwAudioVocAdpcmMode stream_mode;
                if (source->channels != 1u ||
                    !qw_audio_voc_adpcm_mode_from_packing_method(
                        packing_method, &packing_mode) ||
                    !qw_audio_voc_codec_adpcm_mode(
                        source->codec, source->bits_per_sample,
                        &stream_mode) ||
                    packing_mode != stream_mode) {
                    return QW_STATUS_ERR_FORMAT;
                }
                block_align = 0u;
                has_initial_adpcm_sample = 1;
            } else if (source->codec == QW_AUDIO_VOC_CODEC_PCM_U8 &&
                       source->bits_per_sample == 8u &&
                       packing_method == 0u) {
                block_align = source->channels;
            } else {
                return QW_STATUS_ERR_FORMAT;
            }
            data_offset = AUDIO_VOC_LEGACY_DATA_HEADER_SIZE;
            break;
        }
        case QW_AUDIO_VOC_BLOCK_SOUND_CONTINUATION:
            if (source->channels != 1u) return QW_STATUS_ERR_FORMAT;
            if (qw_audio_voc_codec_is_adpcm(source->codec)) {
                QwAudioVocAdpcmMode mode;
                if (!qw_audio_voc_codec_adpcm_mode(
                        source->codec, source->bits_per_sample, &mode)) {
                    return QW_STATUS_ERR_FORMAT;
                }
                block_align = 0u;
            } else if (source->codec == QW_AUDIO_VOC_CODEC_PCM_U8 &&
                       source->bits_per_sample == 8u) {
                block_align = source->channels;
            } else {
                return QW_STATUS_ERR_FORMAT;
            }
            data_offset = 0u;
            break;
        case QW_AUDIO_VOC_BLOCK_SILENCE: {
            uint32_t sample_rate;
            uint32_t total_samples;
            if (block.payload_size != 3u) return QW_STATUS_ERR_FORMAT;
            sample_rate = 1000000u /
                (256u - (uint32_t)block.payload[2u]);
            if (source->codec != QW_AUDIO_VOC_CODEC_PCM_U8 ||
                source->bits_per_sample != 8u || source->channels != 1u ||
                sample_rate != source->sample_rate) {
                return QW_STATUS_ERR_FORMAT;
            }
            total_samples = (uint32_t)qw_read_u16_le(block.payload) + 1u;
            frame->sample_count = total_samples >
                    QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES
                ? QW_AUDIO_VOC_SILENCE_PACKET_SAMPLES : total_samples;
            frame->silence_remaining = total_samples - frame->sample_count;
            frame->next_position = block.next_offset;
            frame->is_silence = 1;
            cursor->position = block.next_offset;
            return 1;
        }
        case QW_AUDIO_VOC_BLOCK_MARKER:
        case QW_AUDIO_VOC_BLOCK_TEXT:
        case QW_AUDIO_VOC_BLOCK_EXTRA_INFO:
            position = block.next_offset;
            cursor->position = position;
            continue;
        default:
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (block.payload_size < data_offset) return QW_STATUS_ERR_FORMAT;
        data_size = block.payload_size - data_offset;
        if (!data_size) return QW_STATUS_ERR_FORMAT;
        if (qw_audio_voc_codec_is_adpcm(source->codec)) {
            QwAudioVocAdpcmMode mode;
            rc = qw_audio_voc_codec_adpcm_mode(
                     source->codec, source->bits_per_sample, &mode)
                ? qw_audio_voc_adpcm_sample_count(
                    mode, data_size, source->channels,
                    has_initial_adpcm_sample,
                    &frame->sample_count)
                : QW_STATUS_ERR_FORMAT;
            if (rc != QW_STATUS_OK) return rc;
        } else {
            if (!block_align || data_size % block_align != 0u ||
                data_size / block_align > UINT32_MAX) {
                return QW_STATUS_ERR_FORMAT;
            }
            frame->sample_count = (uint32_t)(data_size / block_align);
        }
        frame->data = block.payload + data_offset;
        frame->size = data_size;
        frame->next_position = block.next_offset;
        cursor->position = block.next_offset;
        return 1;
    }
    return cursor->loop_depth ? QW_STATUS_ERR_FORMAT : 0;
}
