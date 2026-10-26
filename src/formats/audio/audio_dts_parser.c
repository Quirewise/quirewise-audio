#include "audio_dts_parser.h"
#include "audio_dts_bitstream.h"
#include "audio_dts_exss_parser.h"

#include "quirewise/status.h"

#include <string.h>

enum {
    DTS_CORE_EXTENSION_XCH = 0,
    DTS_CORE_EXTENSION_X96 = 2,
    DTS_CORE_EXTENSION_XXCH = 6
};

typedef struct QwAudioDtsFrameHeader {
    uint32_t frame_size;
    uint32_t sample_rate;
    uint32_t sample_count;
    uint16_t channels;
    int little_endian;
    int packed_14bit;
} QwAudioDtsFrameHeader;

typedef struct QwAudioDtsFrameLayout {
    uint32_t frame_size;
    uint32_t sample_count;
} QwAudioDtsFrameLayout;

static const uint32_t dts_sample_rates[16] = {
    0u, 8000u, 16000u, 32000u, 0u, 0u, 11025u, 22050u,
    44100u, 0u, 0u, 12000u, 24000u, 48000u, 96000u, 192000u
};
static const uint8_t dts_channels[10] = {
    1u, 2u, 2u, 2u, 2u, 3u, 3u, 4u, 4u, 5u
};

static int dts_parse_frame_layout(QwAudioDtsBitReader *reader,
                                  QwAudioDtsFrameLayout *layout)
{
    uint32_t sync;
    uint32_t normal_frame;
    uint32_t deficit_samples;
    uint32_t crc_present;
    uint32_t pcm_blocks;
    uint32_t frame_size;
    int rc;

    if (!reader || !layout) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dts_bit_reader_read(reader, 32u, &sync);
    if (rc != QW_STATUS_OK || sync != UINT32_C(0x7ffe8001))
        return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &normal_frame);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 5u, &deficit_samples);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &crc_present);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 7u, &pcm_blocks);
    if (rc != QW_STATUS_OK) return rc;
    ++pcm_blocks;
    rc = qw_audio_dts_bit_reader_read(reader, 14u, &frame_size);
    if (rc != QW_STATUS_OK) return rc;
    ++frame_size;

    if (deficit_samples != 31u || (pcm_blocks & 7u) != 0u ||
        frame_size < 96u) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    layout->frame_size = frame_size;
    layout->sample_count = pcm_blocks * 32u;
    (void)normal_frame;
    (void)crc_present;
    return QW_STATUS_OK;
}

static int dts_parse_audio_properties(QwAudioDtsBitReader *reader,
                                      QwAudioDtsFrameHeader *header)
{
    uint32_t audio_mode;
    uint32_t sample_rate_code;
    uint32_t bitrate_code;
    uint32_t reserved;
    uint32_t drc_present;
    uint32_t timestamp_present;
    uint32_t auxiliary_data_present;
    uint32_t hdcd_master;
    uint32_t extension_type;
    uint32_t extension_present;
    uint32_t sync_ssf;
    uint32_t lfe_code;
    uint32_t predictor_history;
    int rc;

    if (!reader || !header) return QW_STATUS_ERR_INVALID_ARGUMENT;
    rc = qw_audio_dts_bit_reader_read(reader, 6u, &audio_mode);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 4u, &sample_rate_code);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 5u, &bitrate_code);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &reserved);
    if (rc != QW_STATUS_OK) return rc;
    if (reserved) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &drc_present);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &timestamp_present);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &auxiliary_data_present);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &hdcd_master);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 3u, &extension_type);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &extension_present);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &sync_ssf);
    if (rc != QW_STATUS_OK) return rc;
    rc = qw_audio_dts_bit_reader_read(reader, 2u, &lfe_code);
    if (rc != QW_STATUS_OK) return rc;
    if (lfe_code == 3u) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dts_bit_reader_read(reader, 1u, &predictor_history);
    if (rc != QW_STATUS_OK) return rc;

    if (audio_mode >= sizeof(dts_channels) ||
        !dts_sample_rates[sample_rate_code]) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (extension_present &&
        extension_type != DTS_CORE_EXTENSION_XCH &&
        extension_type != DTS_CORE_EXTENSION_X96 &&
        extension_type != DTS_CORE_EXTENSION_XXCH) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    header->sample_rate = dts_sample_rates[sample_rate_code];
    header->channels = (uint16_t)(dts_channels[audio_mode] +
                                  (lfe_code != 0u));
    (void)bitrate_code;
    (void)drc_present;
    (void)timestamp_present;
    (void)auxiliary_data_present;
    (void)hdcd_master;
    (void)sync_ssf;
    (void)predictor_history;
    return QW_STATUS_OK;
}

static int dts_parse_core_header(const uint8_t *bytes,
                                 size_t size,
                                 QwAudioDtsFrameHeader *header)
{
    QwAudioDtsFrameLayout layout;
    QwAudioDtsBitReader reader;
    int little_endian;
    int packed_14bit;
    int rc;

    if (!bytes || !header || size < 11u) return QW_STATUS_ERR_FORMAT;
    if (bytes[0] == 0x7fu && bytes[1] == 0xfeu &&
        bytes[2] == 0x80u && bytes[3] == 0x01u) {
        little_endian = 0;
        packed_14bit = 0;
    } else if (bytes[0] == 0xfeu && bytes[1] == 0x7fu &&
               bytes[2] == 0x01u && bytes[3] == 0x80u) {
        little_endian = 1;
        packed_14bit = 0;
    } else if (bytes[0] == 0x1fu && bytes[1] == 0xffu &&
               bytes[2] == 0xe8u && bytes[3] == 0x00u) {
        little_endian = 0;
        packed_14bit = 1;
    } else if (bytes[0] == 0xffu && bytes[1] == 0x1fu &&
               bytes[2] == 0x00u && bytes[3] == 0xe8u) {
        little_endian = 1;
        packed_14bit = 1;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }

    if (packed_14bit && size < 14u) return QW_STATUS_ERR_FORMAT;
    rc = qw_audio_dts_bit_reader_init(&reader, bytes, size,
                                      little_endian, packed_14bit);
    if (rc != QW_STATUS_OK) return rc;
    rc = dts_parse_frame_layout(&reader, &layout);
    if (rc != QW_STATUS_OK) return rc;
    rc = dts_parse_audio_properties(&reader, header);
    if (rc != QW_STATUS_OK) return rc;
    header->frame_size = layout.frame_size;
    header->sample_count = layout.sample_count;
    header->little_endian = little_endian;
    header->packed_14bit = packed_14bit;
    return QW_STATUS_OK;
}

int qw_audio_dts_next_frame(const QwAudioDtsSource *source,
                            size_t position,
                            QwAudioDtsFrame *frame)
{
    QwAudioDtsFrameHeader header;
    size_t remaining;
    size_t packet_size;
    int rc;
    if (!source || !frame || !source->data) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(frame, 0, sizeof(*frame));
    if (position == source->frame_data_end) return 0;
    if (position > source->frame_data_end) return QW_STATUS_ERR_FORMAT;
    remaining = source->frame_data_end - position;
    rc = dts_parse_core_header(source->data + position, remaining, &header);
    if (rc != QW_STATUS_OK) return rc;
    if (header.frame_size > remaining) return QW_STATUS_ERR_FORMAT;
    if (header.sample_rate != source->sample_rate ||
        header.channels != source->channels ||
        header.little_endian != source->little_endian ||
        header.packed_14bit != source->packed_14bit) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    packet_size = header.frame_size;
    if (!header.little_endian && !header.packed_14bit) {
#if SIZE_MAX <= UINT32_MAX
        if (header.frame_size > (uint32_t)(SIZE_MAX - 3u))
            return QW_STATUS_ERR_FORMAT;
#endif
        size_t aligned_core_size = (header.frame_size + 3u) & ~(size_t)3u;
        if (aligned_core_size <= remaining &&
            remaining - aligned_core_size >= 4u) {
            size_t extension_size = 0u;
            rc = qw_audio_dts_exss_frame_size(
                source->data + position + aligned_core_size,
                remaining - aligned_core_size,
                &extension_size);
            if (rc == QW_STATUS_OK) {
                packet_size = aligned_core_size + extension_size;
            } else if (rc != QW_STATUS_ERR_UNSUPPORTED) {
                return rc;
            }
        }
    }
    frame->data = source->data + position;
    frame->size = packet_size;
    frame->next_position = position + packet_size;
    frame->sample_count = header.sample_count;
    return 1;
}

int qw_audio_dts_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDtsSource *source,
                       QwConvertReport *report)
{
    QwAudioDtsFrameHeader first_header;
    size_t position = 0u;
    int rc;
    (void)report;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    rc = dts_parse_core_header(bytes, size, &first_header);
    if (rc != QW_STATUS_OK) return rc;
    if (first_header.frame_size > size) return QW_STATUS_ERR_FORMAT;
    source->data = bytes;
    source->size = size;
    source->frame_data_end = size;
    source->sample_rate = first_header.sample_rate;
    source->channels = first_header.channels;
    source->little_endian = first_header.little_endian;
    source->packed_14bit = first_header.packed_14bit;

    while (position < source->frame_data_end) {
        QwAudioDtsFrame frame;
        rc = qw_audio_dts_next_frame(source, position, &frame);
        if (rc == QW_STATUS_ERR_FORMAT && position != 0u)
            return QW_STATUS_ERR_UNSUPPORTED;
        if (rc <= 0) return rc < 0 ? rc : QW_STATUS_ERR_FORMAT;
        if (source->sample_count > UINT64_MAX - frame.sample_count)
            return QW_STATUS_ERR_FORMAT;
        source->sample_count += frame.sample_count;
        position = frame.next_position;
    }
    if (position != source->frame_data_end || !source->sample_count)
        return QW_STATUS_ERR_FORMAT;
    return QW_STATUS_OK;
}
