#include "audio_voc_parser_internal.h"

#include "audio_voc_adpcm_parser.h"
#include "audio_voc_blocks.h"
#include "audio_voc_repeat_parser.h"
#include "core/byte_utils.h"
#include "quirewise/status.h"

#include <string.h>

static int audio_voc_set_format(QwAudioVocSource *source,
                                uint32_t sample_rate,
                                uint16_t channels,
                                uint16_t bits_per_sample,
                                QwAudioVocCodec codec)
{
    if (!sample_rate || sample_rate > 1000000u || channels == 0u ||
        channels > 8u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (source->sample_rate == 0u) {
        source->sample_rate = sample_rate;
        source->channels = channels;
        source->bits_per_sample = bits_per_sample;
        source->codec = codec;
    } else if (source->sample_rate != sample_rate ||
               source->channels != channels ||
               source->bits_per_sample != bits_per_sample ||
               source->codec != codec) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

int qw_audio_voc_codec_adpcm_mode(QwAudioVocCodec codec,
                                  uint16_t bits_per_sample,
                                  QwAudioVocAdpcmMode *mode)
{
    return qw_audio_voc_adpcm_mode_from_codec_tag(
        (uint16_t)codec, bits_per_sample, mode);
}

int qw_audio_voc_codec_is_adpcm(QwAudioVocCodec codec)
{
    return codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4 ||
           codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_3 ||
           codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2 ||
           codec == QW_AUDIO_VOC_CODEC_ADPCM_CT;
}

int qw_audio_voc_codec_has_adpcm_initial_sample(QwAudioVocCodec codec)
{
    return codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4 ||
           codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_3 ||
           codec == QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2;
}

static int audio_voc_add_samples(size_t data_size,
                                 size_t block_align,
                                 uint64_t *sample_count)
{
    size_t samples;
    if (!block_align || !sample_count || !data_size ||
        data_size % block_align != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    samples = data_size / block_align;
    if ((uint64_t)samples > UINT64_MAX - *sample_count)
        return QW_STATUS_ERR_FORMAT;
    *sample_count += (uint64_t)samples;
    return QW_STATUS_OK;
}

static int audio_voc_add_codec_samples(size_t data_size,
                                       uint16_t channels,
                                       uint16_t bits_per_sample,
                                       QwAudioVocCodec codec,
                                       int has_initial_adpcm_sample,
                                       uint64_t *sample_count)
{
    size_t block_align;
    size_t bytes_per_sample;
    uint32_t decoded_samples;
    QwAudioVocAdpcmMode mode;
    int rc;
    if (!sample_count || !channels) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (qw_audio_voc_codec_is_adpcm(codec)) {
        if (!qw_audio_voc_codec_adpcm_mode(codec, bits_per_sample, &mode) ||
            !qw_audio_voc_adpcm_mode_supports_channels(mode, channels)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        rc = qw_audio_voc_adpcm_sample_count(
            mode, data_size, channels, has_initial_adpcm_sample,
            &decoded_samples);
        if (rc != QW_STATUS_OK) return rc;
        if ((uint64_t)decoded_samples > UINT64_MAX - *sample_count)
            return QW_STATUS_ERR_FORMAT;
        *sample_count += decoded_samples;
        return QW_STATUS_OK;
    }
    if (codec != QW_AUDIO_VOC_CODEC_PCM_U8 &&
        codec != QW_AUDIO_VOC_CODEC_PCM_S16LE &&
        codec != QW_AUDIO_VOC_CODEC_PCM_ALAW &&
        codec != QW_AUDIO_VOC_CODEC_PCM_MULAW) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (bits_per_sample == 0u || bits_per_sample % 8u != 0u)
        return QW_STATUS_ERR_UNSUPPORTED;
    bytes_per_sample = bits_per_sample / 8u;
    if ((size_t)channels > SIZE_MAX / bytes_per_sample)
        return QW_STATUS_ERR_FORMAT;
    block_align = (size_t)channels * bytes_per_sample;
    return audio_voc_add_samples(data_size, block_align, sample_count);
}

static int audio_voc_parse_new_data(const uint8_t *bytes,
                                   size_t block_size,
                                   QwAudioVocSource *source,
                                   uint64_t *sample_count)
{
    uint32_t sample_rate;
    uint16_t bits_per_sample;
    uint16_t channels;
    uint16_t codec_tag;
    QwAudioVocCodec codec;
    size_t data_size;
    QwAudioVocAdpcmMode adpcm_mode;
    int rc;
    if (!bytes || !source || !sample_count ||
        block_size < AUDIO_VOC_NEW_DATA_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_rate = qw_read_u32_le(bytes);
    bits_per_sample = bytes[4u];
    channels = bytes[5u];
    codec_tag = qw_read_u16_le(bytes + 6u);
    if (codec_tag == QW_AUDIO_VOC_CODEC_PCM_U8 && bits_per_sample == 8u) {
        codec = QW_AUDIO_VOC_CODEC_PCM_U8;
    } else if (codec_tag == QW_AUDIO_VOC_CODEC_PCM_S16LE &&
               bits_per_sample == 16u) {
        codec = QW_AUDIO_VOC_CODEC_PCM_S16LE;
    } else if (codec_tag == QW_AUDIO_VOC_CODEC_PCM_ALAW &&
               bits_per_sample == 8u) {
        codec = QW_AUDIO_VOC_CODEC_PCM_ALAW;
    } else if (codec_tag == QW_AUDIO_VOC_CODEC_PCM_MULAW &&
               bits_per_sample == 8u) {
        codec = QW_AUDIO_VOC_CODEC_PCM_MULAW;
    } else if (codec_tag >= QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4 &&
               codec_tag <= QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2 &&
               bits_per_sample == 8u &&
               qw_audio_voc_adpcm_mode_from_codec_tag(
                   codec_tag, bits_per_sample, &adpcm_mode)) {
        if (channels != 1u) return QW_STATUS_ERR_UNSUPPORTED;
        codec = (QwAudioVocCodec)codec_tag;
    } else if (codec_tag == QW_AUDIO_VOC_CODEC_ADPCM_CT &&
               bits_per_sample == 16u &&
               qw_audio_voc_adpcm_mode_from_codec_tag(
                   codec_tag, bits_per_sample, &adpcm_mode)) {
        if (channels != 1u) return QW_STATUS_ERR_UNSUPPORTED;
        codec = QW_AUDIO_VOC_CODEC_ADPCM_CT;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    rc = audio_voc_set_format(source, sample_rate, channels,
                              bits_per_sample, codec);
    if (rc != QW_STATUS_OK) return rc;
    data_size = block_size - AUDIO_VOC_NEW_DATA_HEADER_SIZE;
    return audio_voc_add_codec_samples(
        data_size, channels, bits_per_sample, codec,
        qw_audio_voc_codec_has_adpcm_initial_sample(codec), sample_count);
}

static int audio_voc_parse_legacy_data(const uint8_t *bytes,
                                       size_t block_size,
                                       const uint8_t *extra_info,
                                       QwAudioVocSource *source,
                                       uint64_t *sample_count)
{
    uint16_t time_constant;
    uint16_t bits_per_sample = 8u;
    uint8_t packing_method;
    uint32_t sample_rate;
    uint16_t channels = 1u;
    QwAudioVocCodec codec = QW_AUDIO_VOC_CODEC_PCM_U8;
    QwAudioVocAdpcmMode adpcm_mode;
    size_t data_size;
    int rc;
    if (!bytes || !source || !sample_count ||
        block_size < AUDIO_VOC_LEGACY_DATA_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (extra_info) {
        uint32_t denominator;
        if (extra_info[3u] > 1u) return QW_STATUS_ERR_UNSUPPORTED;
        time_constant = qw_read_u16_le(extra_info);
        channels = (uint16_t)extra_info[3u] + 1u;
        packing_method = extra_info[2u];
        denominator = 65536u - (uint32_t)time_constant;
        sample_rate = 256000000u / ((uint32_t)channels * denominator);
    } else {
        time_constant = bytes[0u];
        channels = 1u;
        packing_method = bytes[1u];
        sample_rate = 1000000u / (256u - (uint32_t)time_constant);
    }
    if (packing_method != 0u) {
        if (!qw_audio_voc_adpcm_mode_from_packing_method(
                packing_method, &adpcm_mode) || channels != 1u) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        bits_per_sample =
            qw_audio_voc_adpcm_mode_bits_per_sample(adpcm_mode);
        switch (adpcm_mode) {
        case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_4:
            codec = QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_4;
            break;
        case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_3:
            codec = QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_3;
            break;
        case QW_AUDIO_VOC_ADPCM_MODE_SBPRO_2:
            codec = QW_AUDIO_VOC_CODEC_ADPCM_SBPRO_2;
            break;
        default:
            return QW_STATUS_ERR_UNSUPPORTED;
        }
    }
    rc = audio_voc_set_format(source, sample_rate, channels,
                              bits_per_sample, codec);
    if (rc != QW_STATUS_OK) return rc;
    data_size = block_size - AUDIO_VOC_LEGACY_DATA_HEADER_SIZE;
    return audio_voc_add_codec_samples(
        data_size, channels, bits_per_sample, codec,
        qw_audio_voc_codec_is_adpcm(codec), sample_count);
}

static int audio_voc_parse_continuation(const uint8_t *bytes,
                                        size_t block_size,
                                        const QwAudioVocSource *source,
                                        uint64_t *sample_count)
{
    if (!bytes || !source || !sample_count) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (source->channels != 1u ||
        (source->codec != QW_AUDIO_VOC_CODEC_PCM_U8 &&
         !qw_audio_voc_codec_is_adpcm(source->codec))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return audio_voc_add_codec_samples(
        block_size, source->channels, source->bits_per_sample, source->codec,
        0, sample_count);
}

static int audio_voc_parse_silence(const uint8_t *bytes,
                                   size_t block_size,
                                   QwAudioVocSource *source,
                                   uint64_t *sample_count)
{
    uint32_t sample_rate;
    uint32_t silence_samples;
    int rc;
    if (!bytes || !source || !sample_count) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (block_size != 3u) return QW_STATUS_ERR_FORMAT;
    sample_rate = 1000000u / (256u - (uint32_t)bytes[2u]);
    rc = audio_voc_set_format(source, sample_rate, 1u, 8u,
                              QW_AUDIO_VOC_CODEC_PCM_U8);
    if (rc != QW_STATUS_OK) return rc;
    silence_samples = (uint32_t)qw_read_u16_le(bytes) + 1u;
    return audio_voc_add_samples((size_t)silence_samples, 1u, sample_count);
}

int qw_audio_voc_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioVocSource *source,
                       QwConvertReport *report)
{
    typedef struct QwAudioVocParseLoop {
        uint64_t sample_count_at_start;
        uint32_t additional_repeats;
        int contains_adpcm_initial_sample;
    } QwAudioVocParseLoop;
    size_t position;
    uint64_t sample_count = 0u;
    QwAudioVocParseLoop loops[QW_AUDIO_VOC_MAX_REPEAT_DEPTH] = {{0}};
    size_t loop_depth = 0u;
    int has_metadata = 0;
    int has_terminator = 0;
    int legacy_continuation = 0;
    const uint8_t *pending_extra_info = NULL;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = qw_audio_voc_container_parse_header(bytes, size, &position);
    if (rc != QW_STATUS_OK) return rc;
    source->data_start = position;
    while (position < size) {
        QwAudioVocBlock block;
        rc = qw_audio_voc_block_next(bytes, size, position, size, &block);
        if (rc < 0) return rc;
        if (rc == 0) break;
        if (pending_extra_info &&
            block.type != QW_AUDIO_VOC_BLOCK_SOUND_DATA) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (block.type == QW_AUDIO_VOC_BLOCK_TERMINATOR) {
            if (loop_depth) return QW_STATUS_ERR_FORMAT;
            has_terminator = 1;
            position = block.next_offset;
            source->frame_data_end = block.offset;
            break;
        }
        switch (block.type) {
        case QW_AUDIO_VOC_BLOCK_NEW_DATA:
            if (sample_count &&
                qw_audio_voc_codec_has_adpcm_initial_sample(source->codec))
                return QW_STATUS_ERR_UNSUPPORTED;
            rc = audio_voc_parse_new_data(block.payload, block.payload_size,
                                          source, &sample_count);
            legacy_continuation = 0;
            break;
        case QW_AUDIO_VOC_BLOCK_SOUND_DATA:
            if (sample_count &&
                qw_audio_voc_codec_has_adpcm_initial_sample(source->codec))
                return QW_STATUS_ERR_UNSUPPORTED;
            rc = audio_voc_parse_legacy_data(block.payload,
                                             block.payload_size,
                                             pending_extra_info,
                                             source, &sample_count);
            pending_extra_info = NULL;
            legacy_continuation = rc == QW_STATUS_OK;
            break;
        case QW_AUDIO_VOC_BLOCK_SOUND_CONTINUATION:
            if (!legacy_continuation) return QW_STATUS_ERR_UNSUPPORTED;
            rc = audio_voc_parse_continuation(block.payload,
                                              block.payload_size,
                                              source, &sample_count);
            legacy_continuation = rc == QW_STATUS_OK;
            break;
        case QW_AUDIO_VOC_BLOCK_SILENCE:
            rc = audio_voc_parse_silence(block.payload, block.payload_size,
                                         source, &sample_count);
            legacy_continuation = 0;
            break;
        case QW_AUDIO_VOC_BLOCK_MARKER:
        case QW_AUDIO_VOC_BLOCK_TEXT:
            has_metadata = 1;
            legacy_continuation = 0;
            rc = QW_STATUS_OK;
            break;
        case QW_AUDIO_VOC_BLOCK_EXTRA_INFO:
            if (block.payload_size != 4u) return QW_STATUS_ERR_FORMAT;
            if (block.payload[2u] > 3u || block.payload[3u] > 1u)
                return QW_STATUS_ERR_UNSUPPORTED;
            pending_extra_info = block.payload;
            has_metadata = 1;
            legacy_continuation = 0;
            rc = QW_STATUS_OK;
            break;
        case QW_AUDIO_VOC_BLOCK_REPEAT_START:
            if (loop_depth >= QW_AUDIO_VOC_MAX_REPEAT_DEPTH)
                return QW_STATUS_ERR_UNSUPPORTED;
            rc = qw_audio_voc_repeat_read_count(
                &block, &loops[loop_depth].additional_repeats);
            if (rc != QW_STATUS_OK) return rc;
            loops[loop_depth].sample_count_at_start = sample_count;
            loops[loop_depth].contains_adpcm_initial_sample = 0;
            ++loop_depth;
            has_metadata = 1;
            rc = QW_STATUS_OK;
            break;
        case QW_AUDIO_VOC_BLOCK_REPEAT_END: {
            QwAudioVocParseLoop *loop;
            uint64_t segment_samples;
            rc = qw_audio_voc_repeat_validate_end(&block);
            if (rc != QW_STATUS_OK) return rc;
            if (!loop_depth) return QW_STATUS_ERR_FORMAT;
            loop = &loops[loop_depth - 1u];
            if (sample_count < loop->sample_count_at_start)
                return QW_STATUS_ERR_FORMAT;
            if (loop->additional_repeats &&
                loop->contains_adpcm_initial_sample)
                return QW_STATUS_ERR_UNSUPPORTED;
            segment_samples = sample_count - loop->sample_count_at_start;
            rc = qw_audio_voc_repeat_add_samples(
                &sample_count, segment_samples, loop->additional_repeats);
            if (rc != QW_STATUS_OK) return rc;
            --loop_depth;
            has_metadata = 1;
            break;
        }
        default:
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        if (rc != QW_STATUS_OK) return rc;
        if (loop_depth &&
            qw_audio_voc_codec_has_adpcm_initial_sample(source->codec) &&
            (block.type == QW_AUDIO_VOC_BLOCK_NEW_DATA ||
             block.type == QW_AUDIO_VOC_BLOCK_SOUND_DATA)) {
            size_t loop_index;
            for (loop_index = 0u; loop_index < loop_depth; ++loop_index)
                loops[loop_index].contains_adpcm_initial_sample = 1;
        }
        position = block.next_offset;
    }
    if (loop_depth) return QW_STATUS_ERR_FORMAT;
    if (pending_extra_info) return QW_STATUS_ERR_FORMAT;
    if (!has_terminator) source->frame_data_end = position;
    if (!sample_count || !source->sample_rate || !source->channels)
        return QW_STATUS_ERR_FORMAT;
    source->data = bytes;
    source->size = size;
    source->sample_count = sample_count;
    source->has_metadata = has_metadata ||
                           (has_terminator && position < size);
    return QW_STATUS_OK;
}
