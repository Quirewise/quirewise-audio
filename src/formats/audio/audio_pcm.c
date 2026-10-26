#include "audio_pcm.h"
#include "audio_pcm_parser.h"
#include "../../core/byte_utils.h"

#include "quirewise/status.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void pcm_rate_to_extended80(uint32_t rate, uint8_t *bytes)
{
    uint32_t top_bit = 31u;
    uint64_t significand;
    uint16_t exponent;

    while (top_bit > 0u && ((rate >> top_bit) & 1u) == 0u) {
        --top_bit;
    }
    significand = (uint64_t)rate << (63u - top_bit);
    exponent = (uint16_t)(16383u + top_bit);
    qw_write_u16_be(bytes, exponent);
    qw_write_u32_be(bytes + 2u, (uint32_t)(significand >> 32));
    qw_write_u32_be(bytes + 6u, (uint32_t)significand);
}

static int64_t pcm_read_q31(const uint8_t *sample,
                            uint16_t bits,
                            int little_endian,
                            int unsigned_8bit)
{
    uint32_t raw = 0;
    uint32_t count = (uint32_t)bits / 8u;
    int64_t signed_value;
    uint32_t i;

    if (bits == 8u) {
        return unsigned_8bit ? ((int64_t)sample[0] - 128) * INT64_C(16777216)
                             : ((sample[0] & 0x80u)
                                    ? ((int64_t)sample[0] - 256) * INT64_C(16777216)
                                    : (int64_t)sample[0] * INT64_C(16777216));
    }
    if (little_endian) {
        for (i = 0; i < count; ++i) raw |= (uint32_t)sample[i] << (8u * i);
    } else {
        for (i = 0; i < count; ++i) raw = (raw << 8) | sample[i];
    }
    if (bits == 16u) {
        signed_value = (raw & 0x8000u) ? (int64_t)raw - 65536 : raw;
        return signed_value * 65536;
    }
    if (bits == 24u) {
        signed_value = (raw & 0x800000u) ? (int64_t)raw - 16777216 : raw;
        return signed_value * 256;
    }
    return (raw & 0x80000000u) ? (int64_t)raw - INT64_C(4294967296) : raw;
}

static int64_t pcm_q31_to_integer(int64_t value, uint16_t bits)
{
    int64_t scale = INT64_C(1) << (32u - bits);
    int64_t half = scale / 2;
    int64_t quantized;
    int64_t minimum = -(INT64_C(1) << (bits - 1u));
    int64_t maximum = (INT64_C(1) << (bits - 1u)) - 1;

    if (bits == 32u) {
        quantized = value;
    } else if (value >= 0) {
        quantized = (value + half) / scale;
    } else {
        quantized = -((-value + half) / scale);
    }
    if (quantized < minimum) quantized = minimum;
    if (quantized > maximum) quantized = maximum;
    return quantized;
}

static uint16_t pcm_output_bits(QwOutputFormat format)
{
    switch (format) {
    case QW_FORMAT_WAV:
    case QW_FORMAT_AIFF:
        return 16u;
    case QW_FORMAT_WAV24:
    case QW_FORMAT_AIFF24:
        return 24u;
    case QW_FORMAT_WAV32:
    case QW_FORMAT_AIFF32:
        return 32u;
    default:
        return 0u;
    }
}

static int pcm_output_is_wave(QwOutputFormat format)
{
    return format == QW_FORMAT_WAV || format == QW_FORMAT_WAV24 ||
           format == QW_FORMAT_WAV32;
}

static int pcm_output_is_aiff(QwOutputFormat format)
{
    return format == QW_FORMAT_AIFF || format == QW_FORMAT_AIFF24 ||
           format == QW_FORMAT_AIFF32;
}

static void pcm_write_sample(uint8_t *out,
                             int64_t value_q31,
                             uint16_t bits,
                             int wave)
{
    int64_t sample = pcm_q31_to_integer(value_q31, bits);
    uint32_t count = (uint32_t)bits / 8u;
    uint32_t i;

    if (bits == 8u && wave) {
        out[0] = (uint8_t)(sample + 128);
        return;
    }
    if (wave) {
        uint32_t raw = (uint32_t)sample;
        for (i = 0; i < count; ++i) out[i] = (uint8_t)(raw >> (8u * i));
    } else {
        uint64_t raw = (uint64_t)sample;
        for (i = 0; i < count; ++i) {
            out[count - i - 1u] = (uint8_t)(raw >> (8u * i));
        }
    }
}

static int pcm_encode(const QwAudioPcmSource *source,
                      QwOutputFormat output_format,
                      uint8_t **out_data,
                      size_t *out_size)
{
    uint16_t output_bits = pcm_output_bits(output_format);
    int wave = pcm_output_is_wave(output_format);
    size_t input_frame_bytes;
    size_t output_frame_bytes;
    size_t payload_size;
    size_t header_size = wave ? 44u : 54u;
    size_t total_size;
    uint8_t *result;
    const uint8_t *input;
    uint8_t *output;
    size_t sample_count;
    size_t i;

    if (!source || source->floating_point || !out_data || !out_size ||
        !output_bits ||
        (!wave && !pcm_output_is_aiff(output_format))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *out_data = NULL;
    *out_size = 0u;
    input_frame_bytes = (size_t)source->channels * (source->sample_bits / 8u);
    output_frame_bytes = (size_t)source->channels * (output_bits / 8u);
    if (!input_frame_bytes || !output_frame_bytes ||
        source->sample_bytes % input_frame_bytes != 0u ||
        source->frames > SIZE_MAX / output_frame_bytes) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (wave &&
        (output_frame_bytes > UINT16_MAX ||
         (uint64_t)source->sample_rate * (uint64_t)output_frame_bytes >
             UINT32_MAX)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    sample_count = (size_t)source->frames * source->channels;
    if (sample_count > SIZE_MAX / (output_bits / 8u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    payload_size = sample_count * (output_bits / 8u);
    if (payload_size > UINT32_MAX || payload_size > SIZE_MAX - header_size) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if ((wave && payload_size > UINT32_MAX - 36u) ||
        (!wave && (payload_size > UINT32_MAX - 46u ||
                   source->frames > UINT32_MAX))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    total_size = header_size + payload_size;
    result = (uint8_t *)calloc(1u, total_size);
    if (!result) return QW_STATUS_ERR_OUT_OF_MEMORY;

    if (wave) {
        uint32_t byte_rate = source->sample_rate * (uint32_t)output_frame_bytes;
        memcpy(result, "RIFF", 4u);
        qw_write_u32_le(result + 4u, (uint32_t)(total_size - 8u));
        memcpy(result + 8u, "WAVEfmt ", 8u);
        qw_write_u32_le(result + 16u, 16u);
        qw_write_u16_le(result + 20u, 1u);
        qw_write_u16_le(result + 22u, source->channels);
        qw_write_u32_le(result + 24u, source->sample_rate);
        qw_write_u32_le(result + 28u, byte_rate);
        qw_write_u16_le(result + 32u, (uint16_t)output_frame_bytes);
        qw_write_u16_le(result + 34u, output_bits);
        memcpy(result + 36u, "data", 4u);
        qw_write_u32_le(result + 40u, (uint32_t)payload_size);
    } else {
        memcpy(result, "FORM", 4u);
        qw_write_u32_be(result + 4u, (uint32_t)(total_size - 8u));
        memcpy(result + 8u, "AIFFCOMM", 8u);
        qw_write_u32_be(result + 16u, 18u);
        qw_write_u16_be(result + 20u, source->channels);
        qw_write_u32_be(result + 22u, (uint32_t)source->frames);
        qw_write_u16_be(result + 26u, output_bits);
        pcm_rate_to_extended80(source->sample_rate, result + 28u);
        memcpy(result + 38u, "SSND", 4u);
        qw_write_u32_be(result + 42u, (uint32_t)(payload_size + 8u));
        qw_write_u32_be(result + 46u, 0u);
        qw_write_u32_be(result + 50u, 0u);
    }

    input = source->samples;
    output = result + header_size;
    for (i = 0; i < sample_count; ++i) {
        int64_t sample = pcm_read_q31(input, source->sample_bits,
                                      source->little_endian,
                                      source->unsigned_8bit);
        pcm_write_sample(output, sample, output_bits, wave);
        input += source->sample_bits / 8u;
        output += output_bits / 8u;
    }
    *out_data = result;
    *out_size = total_size;
    return QW_STATUS_OK;
}

int qw_audio_pcm_container_transcode(const void *data,
                                     size_t size,
                                     QwInputFormat input_format,
                                     QwOutputFormat output_format,
                                     size_t audio_track,
                                     uint8_t **out_data,
                                     size_t *out_size,
                                     QwConvertReport *report)
{
    QwAudioPcmSource source;
    int rc;

    if (!data || !size || !out_data || !out_size) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *out_data = NULL;
    *out_size = 0u;
    if (audio_track > 1u) {
        if (report) snprintf(report->note, sizeof(report->note),
                             "WAVE and AIFF contain one audio stream");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!pcm_output_bits(output_format) ||
        (!pcm_output_is_wave(output_format) &&
         !pcm_output_is_aiff(output_format))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    memset(&source, 0, sizeof(source));
    if (input_format == QW_FORMAT_WAV) {
        rc = qw_audio_pcm_parse_wave((const uint8_t *)data, size, &source, report);
    } else if (input_format == QW_FORMAT_AIFF) {
        rc = qw_audio_pcm_parse_aiff((const uint8_t *)data, size, &source, report);
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (rc != QW_STATUS_OK) return rc;
    return pcm_encode(&source, output_format, out_data, out_size);
}

int qw_audio_pcm_container_transcode_stream(const void *data,
                                            size_t size,
                                            QwInputFormat input_format,
                                            QwOutputFormat output_format,
                                            size_t audio_track,
                                            FILE *stream,
                                            size_t max_output_bytes,
                                            size_t *out_size,
                                            QwConvertReport *report)
{
    QwAudioPcmSource source;
    uint8_t header[54] = {0};
    uint8_t chunk[64u * 1024u];
    const uint8_t *input;
    uint16_t output_bits = pcm_output_bits(output_format);
    int wave = pcm_output_is_wave(output_format);
    size_t input_frame_bytes;
    size_t output_frame_bytes;
    size_t payload_size;
    size_t header_size = wave ? 44u : 54u;
    size_t total_size;
    size_t sample_count;
    size_t used = 0u;
    size_t i;
    int rc;

    if (!data || !size || !stream || !out_size) {
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    }
    *out_size = 0u;
    if (audio_track > 1u) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
                                   "WAVE and AIFF contain one audio stream");
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (!output_bits || (!wave && !pcm_output_is_aiff(output_format))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    memset(&source, 0, sizeof(source));
    if (input_format == QW_FORMAT_WAV) {
        rc = qw_audio_pcm_parse_wave((const uint8_t *)data, size, &source, report);
    } else if (input_format == QW_FORMAT_AIFF) {
        rc = qw_audio_pcm_parse_aiff((const uint8_t *)data, size, &source, report);
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (rc != QW_STATUS_OK) return rc;
    if (source.floating_point) return QW_STATUS_ERR_UNSUPPORTED;

    input_frame_bytes = (size_t)source.channels * (source.sample_bits / 8u);
    output_frame_bytes = (size_t)source.channels * (output_bits / 8u);
    if (!input_frame_bytes || !output_frame_bytes ||
        source.sample_bytes % input_frame_bytes != 0u ||
        source.frames > SIZE_MAX / output_frame_bytes ||
        source.frames > SIZE_MAX / source.channels) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_count = (size_t)source.frames * source.channels;
    if (sample_count > SIZE_MAX / (output_bits / 8u)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    payload_size = sample_count * (output_bits / 8u);
    if (payload_size > UINT32_MAX || payload_size > SIZE_MAX - header_size) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if ((wave && payload_size > UINT32_MAX - 36u) ||
        (!wave && (payload_size > UINT32_MAX - 46u ||
                   source.frames > UINT32_MAX))) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    total_size = header_size + payload_size;
    if (max_output_bytes && total_size > max_output_bytes) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
                                   "audio output exceeds the enforced byte limit");
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (wave) {
        uint32_t byte_rate = source.sample_rate * (uint32_t)output_frame_bytes;
        memcpy(header, "RIFF", 4u);
        qw_write_u32_le(header + 4u, (uint32_t)(total_size - 8u));
        memcpy(header + 8u, "WAVEfmt ", 8u);
        qw_write_u32_le(header + 16u, 16u);
        qw_write_u16_le(header + 20u, 1u);
        qw_write_u16_le(header + 22u, source.channels);
        qw_write_u32_le(header + 24u, source.sample_rate);
        qw_write_u32_le(header + 28u, byte_rate);
        qw_write_u16_le(header + 32u, (uint16_t)output_frame_bytes);
        qw_write_u16_le(header + 34u, output_bits);
        memcpy(header + 36u, "data", 4u);
        qw_write_u32_le(header + 40u, (uint32_t)payload_size);
    } else {
        memcpy(header, "FORM", 4u);
        qw_write_u32_be(header + 4u, (uint32_t)(total_size - 8u));
        memcpy(header + 8u, "AIFFCOMM", 8u);
        qw_write_u32_be(header + 16u, 18u);
        qw_write_u16_be(header + 20u, source.channels);
        qw_write_u32_be(header + 22u, (uint32_t)source.frames);
        qw_write_u16_be(header + 26u, output_bits);
        pcm_rate_to_extended80(source.sample_rate, header + 28u);
        memcpy(header + 38u, "SSND", 4u);
        qw_write_u32_be(header + 42u, (uint32_t)(payload_size + 8u));
        qw_write_u32_be(header + 46u, 0u);
        qw_write_u32_be(header + 50u, 0u);
    }
    if (fwrite(header, 1u, header_size, stream) != header_size) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
                                   "could not write PCM audio header");
        return QW_STATUS_ERR_IO;
    }

    input = source.samples;
    for (i = 0u; i < sample_count; ++i) {
        size_t sample_bytes = output_bits / 8u;
        int64_t sample = pcm_read_q31(input, source.sample_bits,
                                      source.little_endian,
                                      source.unsigned_8bit);
        if (used + sample_bytes > sizeof(chunk)) {
            if (fwrite(chunk, 1u, used, stream) != used) {
                if (report) (void)snprintf(report->note, sizeof(report->note),
                                           "could not write PCM audio samples");
                return QW_STATUS_ERR_IO;
            }
            used = 0u;
        }
        pcm_write_sample(chunk + used, sample, output_bits, wave);
        input += source.sample_bits / 8u;
        used += sample_bytes;
        if (used == sizeof(chunk)) {
            if (fwrite(chunk, 1u, used, stream) != used) {
                if (report) (void)snprintf(report->note, sizeof(report->note),
                                           "could not write PCM audio samples");
                return QW_STATUS_ERR_IO;
            }
            used = 0u;
        }
    }
    if (used && fwrite(chunk, 1u, used, stream) != used) {
        if (report) (void)snprintf(report->note, sizeof(report->note),
                                   "could not finish writing PCM audio samples");
        return QW_STATUS_ERR_IO;
    }
    *out_size = total_size;
    return QW_STATUS_OK;
}
