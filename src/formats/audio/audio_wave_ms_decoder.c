#include "audio_wave_ms_decoder.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>

enum {
    AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL = 7u,
    AUDIO_WAVE_MS_BYTES_PER_SAMPLE = 2u
};

static const uint16_t audio_wave_ms_adaptation_table[16] = {
    230u, 230u, 230u, 230u, 307u, 409u, 512u, 614u,
    768u, 614u, 512u, 409u, 307u, 230u, 230u, 230u
};

typedef struct AudioWaveMsChannelState {
    int32_t delta;
    int32_t sample1;
    int32_t sample2;
    int16_t coefficient1;
    int16_t coefficient2;
} AudioWaveMsChannelState;

static int16_t audio_wave_ms_read_i16_le(const uint8_t *bytes)
{
    uint16_t raw = qw_read_u16_le(bytes);
    int32_t value = raw >= 0x8000u ? (int32_t)raw - 65536 : (int32_t)raw;
    return (int16_t)value;
}

static int16_t audio_wave_ms_clip_i16(int64_t value)
{
    if (value < INT16_MIN) value = INT16_MIN;
    if (value > INT16_MAX) value = INT16_MAX;
    return (int16_t)value;
}

static int16_t audio_wave_ms_decode_nibble(AudioWaveMsChannelState *state,
                                           uint8_t nibble)
{
    int32_t signed_nibble = (nibble & 0x08u)
        ? (int32_t)nibble - 16
        : (int32_t)nibble;
    int64_t prediction =
        ((int64_t)state->sample1 * state->coefficient1 +
         (int64_t)state->sample2 * state->coefficient2) / 256;
    int16_t sample = audio_wave_ms_clip_i16(
        prediction + (int64_t)signed_nibble * state->delta);
    int64_t adapted_delta =
        ((int64_t)audio_wave_ms_adaptation_table[nibble] * state->delta) /
        256;

    state->sample2 = state->sample1;
    state->sample1 = sample;
    if (adapted_delta < 16) {
        state->delta = 16;
    } else if (adapted_delta > INT32_MAX / 768) {
        state->delta = INT32_MAX / 768;
    } else {
        state->delta = (int32_t)adapted_delta;
    }
    return sample;
}

static void audio_wave_ms_write_sample(uint8_t *output,
                                       size_t sample_index,
                                       uint16_t channels,
                                       uint16_t channel,
                                       int16_t sample)
{
    size_t interleaved_index = sample_index * (size_t)channels + channel;
    qw_write_u16_le(output + interleaved_index * AUDIO_WAVE_MS_BYTES_PER_SAMPLE,
                    (uint16_t)sample);
}

int qw_audio_wave_ms_decode_frame(const QwAudioWaveMsSource *source,
                                  const QwAudioWaveMsFrame *frame,
                                  uint8_t *pcm_s16le,
                                  size_t pcm_capacity)
{
    AudioWaveMsChannelState channels[6] = {{0}};
    size_t header_size;
    size_t payload_size;
    size_t required_samples;
    size_t required_bytes;
    size_t channel;

    if (!source || !frame || !frame->data || !pcm_s16le ||
        !source->channels || source->channels > 6u ||
        !source->coefficient_count || source->coefficient_count > 256u ||
        !source->coefficients || !source->block_align ||
        !source->samples_per_block || !frame->sample_count ||
        frame->sample_count > source->samples_per_block ||
        frame->size != source->block_align) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    header_size = (size_t)source->channels *
                  AUDIO_WAVE_MS_HEADER_BYTES_PER_CHANNEL;
    if (frame->size < header_size ||
        !qw_checked_size_mul((size_t)frame->sample_count,
                             (size_t)source->channels, &required_samples) ||
        !qw_checked_size_mul(required_samples,
                             AUDIO_WAVE_MS_BYTES_PER_SAMPLE,
                             &required_bytes) ||
        required_bytes > pcm_capacity) {
        return QW_STATUS_ERR_FORMAT;
    }
    payload_size = frame->size - header_size;
    if (source->channels > 2u && payload_size % source->channels != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }

    for (channel = 0u; channel < source->channels; ++channel) {
        uint8_t predictor = frame->data[channel];
        size_t delta_offset = (size_t)source->channels + channel * 2u;
        size_t sample1_offset = (size_t)source->channels * 3u + channel * 2u;
        size_t sample2_offset = (size_t)source->channels * 5u + channel * 2u;
        size_t coefficient_offset;

        if ((uint16_t)predictor >= source->coefficient_count) {
            return QW_STATUS_ERR_FORMAT;
        }
        coefficient_offset = (size_t)predictor * 4u;
        channels[channel].coefficient1 =
            audio_wave_ms_read_i16_le(source->coefficients + coefficient_offset);
        channels[channel].coefficient2 =
            audio_wave_ms_read_i16_le(source->coefficients + coefficient_offset + 2u);
        channels[channel].delta = qw_read_u16_le(frame->data + delta_offset);
        if (channels[channel].delta < 16 ||
            channels[channel].delta > INT16_MAX) {
            return QW_STATUS_ERR_FORMAT;
        }
        channels[channel].sample1 =
            audio_wave_ms_read_i16_le(frame->data + sample1_offset);
        channels[channel].sample2 =
            audio_wave_ms_read_i16_le(frame->data + sample2_offset);
        audio_wave_ms_write_sample(pcm_s16le, 0u, source->channels,
                                   (uint16_t)channel,
                                   (int16_t)channels[channel].sample2);
        if (frame->sample_count > 1u) {
            audio_wave_ms_write_sample(pcm_s16le, 1u, source->channels,
                                       (uint16_t)channel,
                                       (int16_t)channels[channel].sample1);
        }
    }

    if (frame->sample_count <= 2u) return QW_STATUS_OK;
    if (source->channels == 1u) {
        size_t sample_index = 2u;
        size_t byte_index;
        for (byte_index = 0u;
             byte_index < payload_size && sample_index < frame->sample_count;
             ++byte_index) {
            uint8_t packed = frame->data[header_size + byte_index];
            int16_t sample = audio_wave_ms_decode_nibble(&channels[0],
                                                         packed >> 4u);
            audio_wave_ms_write_sample(pcm_s16le, sample_index++, 1u, 0u,
                                       sample);
            if (sample_index < frame->sample_count) {
                sample = audio_wave_ms_decode_nibble(&channels[0],
                                                      packed & 0x0fu);
                audio_wave_ms_write_sample(pcm_s16le, sample_index++, 1u, 0u,
                                           sample);
            }
        }
        return sample_index == frame->sample_count ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
    }
    if (source->channels == 2u) {
        size_t sample_index;
        for (sample_index = 2u; sample_index < frame->sample_count;
             ++sample_index) {
            size_t byte_index = sample_index - 2u;
            uint8_t packed;
            int16_t sample;
            if (byte_index >= payload_size) return QW_STATUS_ERR_FORMAT;
            packed = frame->data[header_size + byte_index];
            sample = audio_wave_ms_decode_nibble(&channels[0], packed >> 4u);
            audio_wave_ms_write_sample(pcm_s16le, sample_index, 2u, 0u,
                                       sample);
            sample = audio_wave_ms_decode_nibble(&channels[1], packed & 0x0fu);
            audio_wave_ms_write_sample(pcm_s16le, sample_index, 2u, 1u,
                                       sample);
        }
        return QW_STATUS_OK;
    }
    {
        size_t bytes_per_channel = payload_size / source->channels;
        for (channel = 0u; channel < source->channels; ++channel) {
            size_t byte_index;
            const uint8_t *channel_data = frame->data + header_size +
                                          channel * bytes_per_channel;
            for (byte_index = 0u; byte_index < bytes_per_channel;
                 ++byte_index) {
                size_t sample_index = 2u + byte_index * 2u;
                uint8_t packed = channel_data[byte_index];
                int16_t sample = audio_wave_ms_decode_nibble(
                    &channels[channel], packed >> 4u);
                if (sample_index < frame->sample_count) {
                    audio_wave_ms_write_sample(pcm_s16le, sample_index,
                                               source->channels,
                                               (uint16_t)channel, sample);
                }
                ++sample_index;
                if (sample_index < frame->sample_count) {
                    sample = audio_wave_ms_decode_nibble(
                        &channels[channel], packed & 0x0fu);
                    audio_wave_ms_write_sample(pcm_s16le, sample_index,
                                               source->channels,
                                               (uint16_t)channel, sample);
                }
            }
        }
    }
    return QW_STATUS_OK;
}
