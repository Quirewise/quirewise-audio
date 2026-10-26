#include "audio_mp4_parser.h"

#include "audio_mp4_aac_config_parser.h"
#include "audio_mp4_composition_parser.h"
#include "audio_mp4_edit_parser.h"
#include "audio_mpeg4_esds_parser.h"
#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

#define MP4_FOURCC(a, b, c, d) \
    (((uint32_t)(uint8_t)(a) << 24u) | ((uint32_t)(uint8_t)(b) << 16u) | \
     ((uint32_t)(uint8_t)(c) << 8u) | (uint32_t)(uint8_t)(d))

enum {
    MP4_BOX_HEADER_SIZE = 8u,
    MP4_BOX_EXTENDED_HEADER_SIZE = 16u,
    MP4_SAMPLE_ENTRY_AUDIO_SIZE = 28u,
    MP4_ALAC_COOKIE_SIZE = 24u,
    MP4_ALAC_EXTRADATA_SIZE = 36u,
    MP4_ALAC_MAX_FRAMES_PER_PACKET = 1048576u,
    MP4_MAX_MDAT_RANGES = 64u
};

typedef struct QwAudioMp4Box {
    size_t offset;
    size_t payload_offset;
    size_t payload_size;
    size_t next_offset;
    uint32_t type;
} QwAudioMp4Box;

typedef struct QwAudioMp4DataRange {
    uint64_t start;
    uint64_t end;
} QwAudioMp4DataRange;

static int mp4_read_box(const uint8_t *data,
                        size_t limit,
                        size_t position,
                        QwAudioMp4Box *box)
{
    uint64_t box_size;
    size_t header_size = MP4_BOX_HEADER_SIZE;
    uint32_t size32;
    if (!data || !box || position > limit ||
        limit - position < MP4_BOX_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    size32 = qw_read_u32_be(data + position);
    box->type = qw_read_u32_be(data + position + 4u);
    if (size32 == 1u) {
        if (limit - position < MP4_BOX_EXTENDED_HEADER_SIZE)
            return QW_STATUS_ERR_FORMAT;
        box_size = qw_read_u64_be(data + position + MP4_BOX_HEADER_SIZE);
        header_size = MP4_BOX_EXTENDED_HEADER_SIZE;
    } else if (size32 == 0u) {
        box_size = (uint64_t)(limit - position);
    } else {
        box_size = size32;
    }
    if (box_size < header_size || box_size > (uint64_t)(limit - position) ||
        box_size > (uint64_t)SIZE_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }
    box->offset = position;
    box->payload_offset = position + header_size;
    box->payload_size = (size_t)box_size - header_size;
    box->next_offset = position + (size_t)box_size;
    return QW_STATUS_OK;
}

static int mp4_find_child_range(const uint8_t *data,
                                size_t start,
                                size_t end,
                                uint32_t type,
                                QwAudioMp4Box *child,
                                int *found)
{
    size_t position;
    int matches = 0;
    if (!data || !child || !found || start > end)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    position = start;
    memset(child, 0, sizeof(*child));
    while (position < end) {
        QwAudioMp4Box current;
        int rc = mp4_read_box(data, end, position, &current);
        if (rc != QW_STATUS_OK || current.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (current.type == type) {
            if (++matches > 1) return QW_STATUS_ERR_FORMAT;
            *child = current;
        }
        position = current.next_offset;
    }
    *found = matches != 0;
    return QW_STATUS_OK;
}

static int mp4_find_child(const uint8_t *data,
                          const QwAudioMp4Box *parent,
                          uint32_t type,
                          QwAudioMp4Box *child,
                          int *found)
{
    if (!parent) return QW_STATUS_ERR_INVALID_ARGUMENT;
    return mp4_find_child_range(data, parent->payload_offset,
                                parent->payload_offset + parent->payload_size,
                                type, child, found);
}

static int mp4_parse_alac_config(const uint8_t *config,
                                 size_t config_size,
                                 uint32_t sample_rate,
                                 uint16_t entry_channels,
                                 QwAudioMp4Source *source)
{
    uint32_t frames_per_packet;
    uint32_t max_frame_bytes;
    uint32_t config_rate;
    uint16_t channels;
    uint16_t bits_per_sample;
    if (!config || !source || config_size != MP4_ALAC_COOKIE_SIZE)
        return QW_STATUS_ERR_FORMAT;
    frames_per_packet = qw_read_u32_be(config);
    bits_per_sample = config[5];
    channels = config[9];
    max_frame_bytes = qw_read_u32_be(config + 12u);
    config_rate = qw_read_u32_be(config + 20u);
    /* ALAC uses zero to mean that the maximum packet size is unknown. */
    if (!frames_per_packet || config[4] != 0u ||
        frames_per_packet > MP4_ALAC_MAX_FRAMES_PER_PACKET ||
        channels == 0u || channels > 2u || channels != entry_channels ||
        (bits_per_sample != 16u && bits_per_sample != 20u &&
         bits_per_sample != 24u && bits_per_sample != 32u) ||
        config_rate != sample_rate ||
        max_frame_bytes > (uint32_t)INT_MAX) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    qw_write_u32_be(source->config, MP4_ALAC_EXTRADATA_SIZE);
    memcpy(source->config + 4u, "alac", 4u);
    memset(source->config + 8u, 0, 4u);
    memcpy(source->config + 12u, config, MP4_ALAC_COOKIE_SIZE);
    source->config_size = MP4_ALAC_EXTRADATA_SIZE;
    source->frames_per_packet = frames_per_packet;
    source->max_frame_bytes = max_frame_bytes;
    source->bits_per_sample = bits_per_sample;
    source->channels = channels;
    source->sample_rate = sample_rate;
    source->codec = QW_AUDIO_MP4_CODEC_ALAC;
    return QW_STATUS_OK;
}

static int mp4_parse_audio_sample_entry_header(const uint8_t *data,
                                              const QwAudioMp4Box *entry,
                                              uint32_t *sample_rate,
                                              uint16_t *channels)
{
    uint16_t data_reference_index;
    uint16_t sound_version;
    uint32_t fixed_sample_rate;
    if (!data || !entry || !sample_rate || !channels ||
        entry->payload_size < MP4_SAMPLE_ENTRY_AUDIO_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    data_reference_index = qw_read_u16_be(data + entry->payload_offset + 6u);
    sound_version = qw_read_u16_be(data + entry->payload_offset + 8u);
    *channels = qw_read_u16_be(data + entry->payload_offset + 16u);
    fixed_sample_rate = qw_read_u32_be(data + entry->payload_offset + 24u);
    *sample_rate = fixed_sample_rate >> 16u;
    if (data_reference_index != 1u || sound_version != 0u ||
        !*channels || !*sample_rate || *channels > 8u ||
        *sample_rate > 384000u || (fixed_sample_rate & 0xffffu)) {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

static int mp4_parse_aac_sample_entry(const uint8_t *data,
                                     const QwAudioMp4Box *entry,
                                     uint32_t sample_rate,
                                     uint16_t channels,
                                     QwAudioMp4Source *source)
{
    QwAudioMp4Box esds;
    const uint8_t *config = NULL;
    size_t config_size = 0u;
    int found;
    int rc;
    if (entry->payload_size <= MP4_SAMPLE_ENTRY_AUDIO_SIZE)
        return QW_STATUS_ERR_FORMAT;
    rc = mp4_find_child_range(
        data, entry->payload_offset + MP4_SAMPLE_ENTRY_AUDIO_SIZE,
        entry->payload_offset + entry->payload_size,
        MP4_FOURCC('e', 's', 'd', 's'), &esds, &found);
    if (rc != QW_STATUS_OK) return rc;
    if (!found || esds.payload_size < 4u) return QW_STATUS_ERR_UNSUPPORTED;
    rc = qw_audio_mpeg4_esds_find_aac_specific_info(
        data + esds.payload_offset + 4u, esds.payload_size - 4u, 0u,
        &config, &config_size);
    if (rc != QW_STATUS_OK) return rc;
    return qw_audio_mp4_parse_aac_config(config, config_size, sample_rate,
                                         channels, source);
}

static int mp4_parse_alac_sample_entry(const uint8_t *data,
                                      const QwAudioMp4Box *entry,
                                      uint32_t sample_rate,
                                      uint16_t channels,
                                      QwAudioMp4Source *source)
{
    QwAudioMp4Box alac;
    const uint8_t *config;
    int found;
    int rc;
    rc = mp4_find_child_range(
        data, entry->payload_offset + MP4_SAMPLE_ENTRY_AUDIO_SIZE,
        entry->payload_offset + entry->payload_size,
        MP4_FOURCC('a', 'l', 'a', 'c'), &alac, &found);
    if (rc != QW_STATUS_OK) return rc;
    if (!found) return QW_STATUS_ERR_UNSUPPORTED;

    /* ALACSpecificConfig is 24 bytes. Some writers include a zero
     * version/flags word in the sample-entry atom, so normalize both forms. */
    if (alac.payload_size == MP4_ALAC_COOKIE_SIZE) {
        config = data + alac.payload_offset;
    } else if (alac.payload_size == 4u + MP4_ALAC_COOKIE_SIZE &&
               qw_read_u32_be(data + alac.payload_offset) == 0u) {
        config = data + alac.payload_offset + 4u;
    } else {
        return QW_STATUS_ERR_FORMAT;
    }
    return mp4_parse_alac_config(config, MP4_ALAC_COOKIE_SIZE, sample_rate,
                                 channels, source);
}

static int mp4_parse_sample_description_entry(
    const uint8_t *data,
    const QwAudioMp4Box *entry,
    QwAudioMp4Source *description)
{
    uint32_t sample_rate;
    uint16_t channels;
    int rc;
    if (!data || !entry || !description ||
        entry->payload_size < MP4_SAMPLE_ENTRY_AUDIO_SIZE)
        return QW_STATUS_ERR_FORMAT;
    rc = mp4_parse_audio_sample_entry_header(data, entry, &sample_rate,
                                             &channels);
    if (rc != QW_STATUS_OK) return rc;
    if (entry->type == MP4_FOURCC('m', 'p', '4', 'a')) {
        return mp4_parse_aac_sample_entry(data, entry, sample_rate, channels,
                                          description);
    }
    if (entry->type == MP4_FOURCC('a', 'l', 'a', 'c')) {
        return mp4_parse_alac_sample_entry(data, entry, sample_rate, channels,
                                           description);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

static int mp4_sample_descriptions_equal(
    const QwAudioMp4Source *left,
    const QwAudioMp4Source *right)
{
    return left->codec == right->codec &&
           left->config_size == right->config_size &&
           left->config_size <= sizeof(left->config) &&
           right->config_size <= sizeof(right->config) &&
           memcmp(left->config, right->config, left->config_size) == 0 &&
           left->frames_per_packet == right->frames_per_packet &&
           left->max_frame_bytes == right->max_frame_bytes &&
           left->sample_rate == right->sample_rate &&
           left->channels == right->channels &&
           left->bits_per_sample == right->bits_per_sample &&
           left->channel_configuration == right->channel_configuration &&
           left->audio_object_type == right->audio_object_type &&
           left->ps_present == right->ps_present;
}

static void mp4_copy_sample_description(QwAudioMp4Source *destination,
                                        const QwAudioMp4Source *description)
{
    destination->frames_per_packet = description->frames_per_packet;
    destination->max_frame_bytes = description->max_frame_bytes;
    destination->sample_rate = description->sample_rate;
    destination->channels = description->channels;
    destination->bits_per_sample = description->bits_per_sample;
    destination->channel_configuration = description->channel_configuration;
    destination->audio_object_type = description->audio_object_type;
    destination->ps_present = description->ps_present;
    memcpy(destination->config, description->config,
           sizeof(destination->config));
    destination->config_size = description->config_size;
    destination->codec = description->codec;
}

static int mp4_parse_sample_description(const uint8_t *data,
                                        const QwAudioMp4Box *stsd,
                                        QwAudioMp4Source *source)
{
    QwAudioMp4Source first_description = {0};
    size_t position;
    size_t end;
    uint32_t entry_count;
    uint32_t index;
    if (!data || !stsd || !source || stsd->payload_size < 8u)
        return QW_STATUS_ERR_FORMAT;
    entry_count = qw_read_u32_be(data + stsd->payload_offset + 4u);
    position = stsd->payload_offset + 8u;
    end = stsd->payload_offset + stsd->payload_size;
    if (!entry_count ||
        (uint64_t)entry_count > (uint64_t)(end - position) /
                                    MP4_BOX_HEADER_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }

    for (index = 0u; index < entry_count; ++index) {
        QwAudioMp4Box entry;
        QwAudioMp4Source description = {0};
        int rc = mp4_read_box(data, end, position, &entry);
        if (rc != QW_STATUS_OK || entry.next_offset <= position ||
            entry.payload_size < MP4_SAMPLE_ENTRY_AUDIO_SIZE) {
            return QW_STATUS_ERR_FORMAT;
        }
        rc = mp4_parse_sample_description_entry(data, &entry, &description);
        if (rc != QW_STATUS_OK) return rc;
        if (index == 0u) {
            first_description = description;
        } else if (!mp4_sample_descriptions_equal(&first_description,
                                                  &description)) {
            return QW_STATUS_ERR_UNSUPPORTED;
        }
        position = entry.next_offset;
    }
    if (position != end) return QW_STATUS_ERR_FORMAT;

    mp4_copy_sample_description(source, &first_description);
    source->sample_description_count = entry_count;
    return QW_STATUS_OK;
}

static int mp4_parse_mdhd(const uint8_t *data,
                          const QwAudioMp4Box *mdhd,
                          uint32_t *timescale)
{
    uint8_t version;
    size_t offset;
    if (!data || !mdhd || !timescale || mdhd->payload_size < 4u)
        return QW_STATUS_ERR_FORMAT;
    version = data[mdhd->payload_offset];
    if (version == 0u) {
        if (mdhd->payload_size < 24u) return QW_STATUS_ERR_FORMAT;
        offset = mdhd->payload_offset + 12u;
    } else if (version == 1u) {
        if (mdhd->payload_size < 36u) return QW_STATUS_ERR_FORMAT;
        offset = mdhd->payload_offset + 20u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *timescale = qw_read_u32_be(data + offset);
    return *timescale ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int mp4_parse_tkhd_track_id(const uint8_t *data,
                                   const QwAudioMp4Box *tkhd,
                                   uint32_t *track_id)
{
    uint8_t version;
    size_t offset;
    if (!data || !tkhd || !track_id || tkhd->payload_size < 4u)
        return QW_STATUS_ERR_FORMAT;
    version = data[tkhd->payload_offset];
    if (version == 0u) {
        offset = 12u;
    } else if (version == 1u) {
        offset = 20u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    if (tkhd->payload_size < offset + 4u) return QW_STATUS_ERR_FORMAT;
    *track_id = qw_read_u32_be(data + tkhd->payload_offset + offset);
    return *track_id ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int mp4_parse_mvhd(const uint8_t *data,
                          const QwAudioMp4Box *mvhd,
                          uint32_t *timescale)
{
    uint8_t version;
    size_t offset;
    if (!data || !mvhd || !timescale || mvhd->payload_size < 4u)
        return QW_STATUS_ERR_FORMAT;
    version = data[mvhd->payload_offset];
    if (version == 0u) {
        if (mvhd->payload_size < 20u) return QW_STATUS_ERR_FORMAT;
        offset = mvhd->payload_offset + 12u;
    } else if (version == 1u) {
        if (mvhd->payload_size < 32u) return QW_STATUS_ERR_FORMAT;
        offset = mvhd->payload_offset + 20u;
    } else {
        return QW_STATUS_ERR_UNSUPPORTED;
    }
    *timescale = qw_read_u32_be(data + offset);
    return *timescale ? QW_STATUS_OK : QW_STATUS_ERR_FORMAT;
}

static int mp4_set_fullbox_table(const uint8_t *data,
                                 size_t size,
                                 const QwAudioMp4Box *box,
                                 uint32_t entry_size,
                                 size_t *entries_offset,
                                 size_t *entry_count)
{
    uint32_t count;
    size_t bytes_needed;
    if (!data || !box || !entries_offset || !entry_count ||
        box->payload_size < 8u) {
        return QW_STATUS_ERR_FORMAT;
    }
    count = qw_read_u32_be(data + box->payload_offset + 4u);
    if ((size_t)count > (SIZE_MAX - 8u) / entry_size)
        return QW_STATUS_ERR_FORMAT;
    bytes_needed = 8u + (size_t)count * entry_size;
    if (bytes_needed != box->payload_size ||
        box->payload_offset > size ||
        bytes_needed > size - box->payload_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    *entries_offset = box->payload_offset + 8u;
    *entry_count = count;
    return QW_STATUS_OK;
}

static int mp4_sample_size_table_bytes(uint32_t sample_count,
                                       uint8_t field_bits,
                                       size_t *table_bytes)
{
    size_t count = (size_t)sample_count;
    if (!table_bytes) return QW_STATUS_ERR_INVALID_ARGUMENT;
    switch (field_bits) {
    case 4u:
        *table_bytes = count / 2u + count % 2u;
        return QW_STATUS_OK;
    case 8u:
        *table_bytes = count;
        return QW_STATUS_OK;
    case 16u:
        if (count > SIZE_MAX / 2u) return QW_STATUS_ERR_FORMAT;
        *table_bytes = count * 2u;
        return QW_STATUS_OK;
    case 32u:
        if (count > SIZE_MAX / 4u) return QW_STATUS_ERR_FORMAT;
        *table_bytes = count * 4u;
        return QW_STATUS_OK;
    default:
        return QW_STATUS_ERR_UNSUPPORTED;
    }
}

static int mp4_parse_sample_sizes(const uint8_t *data,
                                  size_t size,
                                  const QwAudioMp4Box *stsz,
                                  const QwAudioMp4Box *stz2,
                                  QwAudioMp4Source *source)
{
    uint32_t sample_count;
    uint8_t field_bits;
    size_t sample_table_bytes;
    size_t expected_size;
    size_t payload_offset;
    size_t payload_size;
    int rc;
    if (!data || !source || ((stsz != NULL) == (stz2 != NULL)))
        return QW_STATUS_ERR_FORMAT;
    if (stsz) {
        if (stsz->payload_size < 12u) return QW_STATUS_ERR_FORMAT;
        payload_offset = stsz->payload_offset;
        payload_size = stsz->payload_size;
        source->fixed_sample_size =
            qw_read_u32_be(data + payload_offset + 4u);
        sample_count = qw_read_u32_be(data + payload_offset + 8u);
        field_bits = 32u;
        sample_table_bytes = 0u;
        if (!source->fixed_sample_size) {
            rc = mp4_sample_size_table_bytes(sample_count, field_bits,
                                             &sample_table_bytes);
            if (rc != QW_STATUS_OK) return rc;
        }
    } else {
        const uint8_t *payload;
        if (stz2->payload_size < 12u) return QW_STATUS_ERR_FORMAT;
        payload_offset = stz2->payload_offset;
        payload_size = stz2->payload_size;
        payload = data + payload_offset;
        if (qw_read_u32_be(payload) != 0u ||
            payload[4u] != 0u || payload[5u] != 0u || payload[6u] != 0u) {
            return QW_STATUS_ERR_FORMAT;
        }
        field_bits = payload[7u];
        if (field_bits != 4u && field_bits != 8u && field_bits != 16u &&
            field_bits != 32u)
            return QW_STATUS_ERR_UNSUPPORTED;
        sample_count = qw_read_u32_be(payload + 8u);
        source->fixed_sample_size = 0u;
        rc = mp4_sample_size_table_bytes(sample_count, field_bits,
                                         &sample_table_bytes);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (sample_table_bytes > SIZE_MAX - 12u) return QW_STATUS_ERR_FORMAT;
    expected_size = 12u + (source->fixed_sample_size
        ? 0u : sample_table_bytes);
    if (expected_size != payload_size || payload_offset > size ||
        expected_size > size - payload_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->sample_count = sample_count;
    source->sample_size_field_bits = field_bits;
    source->sample_size_entries_offset = payload_offset + 12u;
    return QW_STATUS_OK;
}

static uint32_t mp4_sample_size_at(const QwAudioMp4Source *source,
                                   size_t sample_index)
{
    const uint8_t *entry;
    if (source->fixed_sample_size) return source->fixed_sample_size;
    entry = source->data + source->sample_size_entries_offset;
    switch (source->sample_size_field_bits) {
    case 4u: {
        uint8_t packed = entry[sample_index / 2u];
        return (sample_index & 1u) ? packed & 0x0fu : packed >> 4u;
    }
    case 8u:
        return entry[sample_index];
    case 16u:
        return qw_read_u16_be(entry + sample_index * 2u);
    case 32u:
        return qw_read_u32_be(entry + sample_index * 4u);
    default:
        return 0u;
    }
}

static uint64_t mp4_chunk_offset_at(const QwAudioMp4Source *source,
                                    size_t chunk_index)
{
    const uint8_t *entry =
        source->data + source->chunk_entries_offset +
        chunk_index * source->chunk_offset_size;
    return source->chunk_offset_size == 8u
        ? qw_read_u64_be(entry) : qw_read_u32_be(entry);
}

static int mp4_range_in_mdat(const QwAudioMp4DataRange *ranges,
                             size_t range_count,
                             uint64_t offset,
                             uint64_t length)
{
    size_t low = 0u;
    size_t high = range_count;
    const QwAudioMp4DataRange *range;
    if (!ranges || !range_count) return 0;
    while (low < high) {
        size_t middle = low + (high - low) / 2u;
        if (ranges[middle].start <= offset) low = middle + 1u;
        else high = middle;
    }
    if (!low) return 0;
    range = &ranges[low - 1u];
    return offset <= range->end && length <= range->end - offset;
}

static int mp4_validate_sample_tables(const uint8_t *data,
                                     QwAudioMp4Source *source,
                                     const QwAudioMp4DataRange *mdat_ranges,
                                     size_t mdat_range_count)
{
    uint64_t timed_samples = 0u;
    uint64_t total_duration = 0u;
    uint64_t mapped_samples = 0u;
    size_t sample_index = 0u;
    size_t stsc_index = 0u;
    size_t chunk_index;
    size_t i;
    uint64_t total_sample_bytes = 0u;
    if (!source->sample_count ||
        source->sample_count > QW_AUDIO_MP4_MAX_SAMPLES ||
        !source->stts_entry_count ||
        !source->stsc_entry_count || !source->chunk_count ||
        source->stsc_entry_count > source->chunk_count) {
        return QW_STATUS_ERR_FORMAT;
    }

    for (i = 0u; i < source->stts_entry_count; ++i) {
        const uint8_t *entry =
            data + source->stts_entries_offset + i * 8u;
        uint32_t count = qw_read_u32_be(entry);
        uint32_t duration = qw_read_u32_be(entry + 4u);
        if (!count || !duration ||
            (source->codec == QW_AUDIO_MP4_CODEC_ALAC &&
             duration > source->frames_per_packet) ||
            (uint64_t)count > UINT64_MAX - timed_samples ||
            (uint64_t)count > (UINT64_MAX - total_duration) / duration) {
            return QW_STATUS_ERR_FORMAT;
        }
        timed_samples += count;
        total_duration += (uint64_t)count * duration;
    }
    if (timed_samples != (uint64_t)source->sample_count ||
        total_duration > (uint64_t)INT64_MAX) {
        return QW_STATUS_ERR_FORMAT;
    }

    for (i = 0u; i < source->stsc_entry_count; ++i) {
        const uint8_t *entry =
            data + source->stsc_entries_offset + i * 12u;
        uint32_t first_chunk = qw_read_u32_be(entry);
        uint32_t samples_per_chunk = qw_read_u32_be(entry + 4u);
        uint32_t description_index = qw_read_u32_be(entry + 8u);
        if (!first_chunk || !samples_per_chunk || !description_index ||
            description_index > source->sample_description_count ||
            (i == 0u && first_chunk != 1u) ||
            (i > 0u && first_chunk <= qw_read_u32_be(
                data + source->stsc_entries_offset + (i - 1u) * 12u)) ||
            first_chunk > source->chunk_count) {
            return QW_STATUS_ERR_FORMAT;
        }
    }

    for (chunk_index = 0u; chunk_index < source->chunk_count; ++chunk_index) {
        const uint8_t *mapping;
        uint32_t first_chunk;
        uint32_t samples_per_chunk;
        uint64_t chunk_bytes = 0u;
        uint64_t chunk_offset;
        uint32_t j;
        while (stsc_index + 1u < source->stsc_entry_count) {
            const uint8_t *next_entry = data + source->stsc_entries_offset +
                (stsc_index + 1u) * 12u;
            if (qw_read_u32_be(next_entry) > chunk_index + 1u) break;
            ++stsc_index;
        }
        mapping = data + source->stsc_entries_offset + stsc_index * 12u;
        first_chunk = qw_read_u32_be(mapping);
        samples_per_chunk = qw_read_u32_be(mapping + 4u);
        if (first_chunk > chunk_index + 1u ||
            samples_per_chunk > source->sample_count - sample_index) {
            return QW_STATUS_ERR_FORMAT;
        }
        for (j = 0u; j < samples_per_chunk; ++j) {
            uint32_t sample_size;
            if (sample_index >= source->sample_count) return QW_STATUS_ERR_FORMAT;
            sample_size = mp4_sample_size_at(source, sample_index++);
            if (!sample_size || (uint64_t)sample_size > UINT64_MAX - chunk_bytes)
                return QW_STATUS_ERR_FORMAT;
            chunk_bytes += sample_size;
        }
        chunk_offset = mp4_chunk_offset_at(source, chunk_index);
        if (!chunk_bytes ||
            !mp4_range_in_mdat(mdat_ranges, mdat_range_count, chunk_offset,
                               chunk_bytes)) {
            return QW_STATUS_ERR_FORMAT;
        }
        if (chunk_bytes > UINT64_MAX - total_sample_bytes)
            return QW_STATUS_ERR_FORMAT;
        total_sample_bytes += chunk_bytes;
        if ((uint64_t)samples_per_chunk > UINT64_MAX - mapped_samples)
            return QW_STATUS_ERR_FORMAT;
        mapped_samples += samples_per_chunk;
    }
    if (sample_index != source->sample_count ||
        mapped_samples != (uint64_t)source->sample_count) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->decoded_sample_count = total_duration;
    source->total_sample_bytes = total_sample_bytes;
    return QW_STATUS_OK;
}

static int mp4_parse_audio_track(const uint8_t *data,
                                 size_t size,
                                 const QwAudioMp4Box *trak,
                                 const QwAudioMp4DataRange *mdat_ranges,
                                 size_t mdat_range_count,
                                 uint32_t movie_timescale,
                                 int parse_details,
                                 int is_fragmented,
                                 QwAudioMp4Source *source,
                                 uint32_t *track_id_out,
                                 int *is_audio)
{
    QwAudioMp4Box mdia;
    QwAudioMp4Box hdlr;
    QwAudioMp4Box mdhd;
    QwAudioMp4Box minf;
    QwAudioMp4Box stbl;
    QwAudioMp4Box stsd;
    QwAudioMp4Box stts;
    QwAudioMp4Box stsc;
    QwAudioMp4Box stsz;
    QwAudioMp4Box stz2;
    QwAudioMp4Box stco;
    QwAudioMp4Box co64;
    QwAudioMp4Box edts;
    QwAudioMp4Box elst;
    QwAudioMp4Box ctts;
    QwAudioMp4Box tkhd;
    uint32_t table_count;
    int found_mdia;
    int found_hdlr;
    int found_mdhd;
    int found_minf;
    int found_stbl;
    int found_stsd;
    int found_stts;
    int found_stsc;
    int found_stsz;
    int found_stz2;
    int found_stco;
    int found_co64;
    int found_edts;
    int found_elst = 0;
    int found_ctts;
    int found_tkhd;
    int rc;
    if (!data || !trak || !source || !track_id_out || !is_audio)
        return QW_STATUS_ERR_INVALID_ARGUMENT;
    *is_audio = 0;
    *track_id_out = 0u;
    rc = mp4_find_child(data, trak, MP4_FOURCC('e', 'd', 't', 's'),
                        &edts, &found_edts);
    if (rc != QW_STATUS_OK) return rc;
    if (found_edts) {
        rc = mp4_find_child(data, &edts, MP4_FOURCC('e', 'l', 's', 't'),
                            &elst, &found_elst);
        if (rc != QW_STATUS_OK) return rc;
        if (!found_elst) return QW_STATUS_ERR_FORMAT;
    }
    rc = mp4_find_child(data, trak, MP4_FOURCC('m', 'd', 'i', 'a'),
                        &mdia, &found_mdia);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_mdia) return QW_STATUS_ERR_FORMAT;
    rc = mp4_find_child(data, &mdia, MP4_FOURCC('h', 'd', 'l', 'r'),
                        &hdlr, &found_hdlr);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_hdlr || hdlr.payload_size < 12u) return QW_STATUS_ERR_FORMAT;
    if (qw_read_u32_be(data + hdlr.payload_offset + 8u) !=
        MP4_FOURCC('s', 'o', 'u', 'n')) {
        return QW_STATUS_OK;
    }
    *is_audio = 1;
    if (!parse_details) return QW_STATUS_OK;

    rc = mp4_find_child(data, &mdia, MP4_FOURCC('m', 'd', 'h', 'd'),
                        &mdhd, &found_mdhd);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &mdia, MP4_FOURCC('m', 'i', 'n', 'f'),
                        &minf, &found_minf);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_mdhd || !found_minf) return QW_STATUS_ERR_FORMAT;
    rc = mp4_parse_mdhd(data, &mdhd, &source->media_timescale);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &minf, MP4_FOURCC('s', 't', 'b', 'l'),
                        &stbl, &found_stbl);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_stbl) return QW_STATUS_ERR_FORMAT;

    rc = mp4_find_child(data, &stbl, MP4_FOURCC('c', 't', 't', 's'),
                        &ctts, &found_ctts);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 's', 'd'),
                        &stsd, &found_stsd);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 't', 's'),
                        &stts, &found_stts);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 's', 'c'),
                        &stsc, &found_stsc);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 's', 'z'),
                        &stsz, &found_stsz);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 'z', '2'),
                        &stz2, &found_stz2);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('s', 't', 'c', 'o'),
                        &stco, &found_stco);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_find_child(data, &stbl, MP4_FOURCC('c', 'o', '6', '4'),
                        &co64, &found_co64);
    if (rc != QW_STATUS_OK) return rc;
    if (!found_stsd) return QW_STATUS_ERR_FORMAT;
    source->data = data;
    source->size = size;
    rc = mp4_parse_sample_description(data, &stsd, source);
    if (rc != QW_STATUS_OK) return rc;
    if (source->media_timescale != source->sample_rate)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (is_fragmented) {
        size_t entries_offset;
        size_t entry_count;
        rc = mp4_find_child(data, trak, MP4_FOURCC('t', 'k', 'h', 'd'),
                            &tkhd, &found_tkhd);
        if (rc != QW_STATUS_OK) return rc;
        if (!found_tkhd) return QW_STATUS_ERR_FORMAT;
        rc = mp4_parse_tkhd_track_id(data, &tkhd, track_id_out);
        if (rc != QW_STATUS_OK) return rc;
        if (found_edts) return QW_STATUS_ERR_UNSUPPORTED;
        if (found_ctts) {
            rc = mp4_set_fullbox_table(data, size, &ctts, 8u,
                                       &entries_offset, &entry_count);
            if (rc != QW_STATUS_OK) return rc;
            if (entry_count) return QW_STATUS_ERR_UNSUPPORTED;
        }
        source->is_fragmented = 1u;
        return QW_STATUS_OK;
    }
    if (!found_stts || !found_stsc || found_stsz == found_stz2 ||
        found_stco == found_co64) {
        return QW_STATUS_ERR_FORMAT;
    }
    rc = mp4_parse_sample_sizes(data, size,
                                found_stsz ? &stsz : NULL,
                                found_stz2 ? &stz2 : NULL,
                                source);
    if (rc != QW_STATUS_OK) return rc;
    if (found_ctts) {
        rc = qw_audio_mp4_parse_composition_offsets(
            data + ctts.payload_offset, ctts.payload_size,
            source->sample_count, &source->composition_table);
        if (rc != QW_STATUS_OK) return rc;
        source->has_composition_offsets = 1u;
        /* Edit-list trimming maps through one scalar media-time origin.
         * Without an edit list, next_sample applies each ctts run directly. */
        if (found_edts) {
            rc = qw_audio_mp4_composition_offsets_constant(
                &source->composition_table, &source->composition_offset);
            if (rc != QW_STATUS_OK) return rc;
        }
    }
    rc = mp4_set_fullbox_table(data, size, &stts, 8u,
                               &source->stts_entries_offset,
                               &source->stts_entry_count);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_set_fullbox_table(data, size, &stsc, 12u,
                               &source->stsc_entries_offset,
                               &source->stsc_entry_count);
    if (rc != QW_STATUS_OK) return rc;
    rc = mp4_set_fullbox_table(data, size,
                               found_stco ? &stco : &co64,
                               found_stco ? 4u : 8u,
                               &source->chunk_entries_offset,
                               &source->chunk_count);
    if (rc != QW_STATUS_OK) return rc;
    table_count = found_stco ? 4u : 8u;
    source->chunk_offset_size = (uint8_t)table_count;
    rc = mp4_validate_sample_tables(data, source, mdat_ranges,
                                    mdat_range_count);
    if (rc != QW_STATUS_OK) return rc;
    source->presentation_sample_count = source->decoded_sample_count;
    if (found_edts) {
        rc = qw_audio_mp4_parse_edit_list(
            data + elst.payload_offset, elst.payload_size,
            movie_timescale, source->media_timescale, source->sample_rate,
            source->decoded_sample_count, source->composition_offset,
            &source->trim_start_samples,
            &source->presentation_sample_count,
            &source->leading_silence_samples);
        if (rc != QW_STATUS_OK) return rc;
        if (source->presentation_sample_count > QW_AUDIO_MP4_MAX_SAMPLES)
            return QW_STATUS_ERR_UNSUPPORTED;
    }
    return QW_STATUS_OK;
}

int qw_audio_mp4_parse(const uint8_t *bytes,
                       size_t size,
                       size_t requested_audio_track,
                       QwAudioMp4Source *source,
                       QwConvertReport *report)
{
    QwAudioMp4Box moov;
    QwAudioMp4Box mvhd;
    QwAudioMp4DataRange mdat_ranges[MP4_MAX_MDAT_RANGES];
    size_t position = 0u;
    size_t trak_position;
    size_t moov_count = 0u;
    size_t mdat_range_count = 0u;
    size_t audio_track_count = 0u;
    size_t requested_audio_index = requested_audio_track
        ? requested_audio_track - 1u : 0u;
    uint32_t selected_track_id = 0u;
    uint32_t movie_timescale = 0u;
    int found_mvhd;
    int saw_fragment = 0;
    int rc;
    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    memset(&moov, 0, sizeof(moov));
    while (position < size) {
        QwAudioMp4Box box;
        rc = mp4_read_box(bytes, size, position, &box);
        if (rc != QW_STATUS_OK || box.next_offset <= position)
            return QW_STATUS_ERR_FORMAT;
        if (box.type == MP4_FOURCC('m', 'o', 'o', 'v')) {
            if (++moov_count > 1u) return QW_STATUS_ERR_UNSUPPORTED;
            moov = box;
        } else if (box.type == MP4_FOURCC('m', 'd', 'a', 't')) {
            if (mdat_range_count >= MP4_MAX_MDAT_RANGES)
                return QW_STATUS_ERR_UNSUPPORTED;
            mdat_ranges[mdat_range_count].start = box.payload_offset;
            mdat_ranges[mdat_range_count].end = box.next_offset;
            ++mdat_range_count;
        } else if (box.type == MP4_FOURCC('m', 'o', 'o', 'f')) {
            saw_fragment = 1;
        }
        position = box.next_offset;
    }
    if (!moov_count || !mdat_range_count) return QW_STATUS_ERR_UNSUPPORTED;
    rc = mp4_find_child(bytes, &moov, MP4_FOURCC('m', 'v', 'h', 'd'),
                        &mvhd, &found_mvhd);
    if (rc != QW_STATUS_OK) return rc;
    if (found_mvhd) {
        rc = mp4_parse_mvhd(bytes, &mvhd, &movie_timescale);
        if (rc != QW_STATUS_OK) return rc;
    }

    trak_position = moov.payload_offset;
    while (trak_position < moov.payload_offset + moov.payload_size) {
        QwAudioMp4Box child;
        int is_audio = 0;
        rc = mp4_read_box(bytes, moov.payload_offset + moov.payload_size,
                          trak_position, &child);
        if (rc != QW_STATUS_OK || child.next_offset <= trak_position)
            return QW_STATUS_ERR_FORMAT;
        if (child.type == MP4_FOURCC('t', 'r', 'a', 'k')) {
            QwAudioMp4Source candidate = {0};
            uint32_t candidate_track_id = 0u;
            rc = mp4_parse_audio_track(bytes, size, &child, mdat_ranges,
                                       mdat_range_count, movie_timescale,
                                       audio_track_count == requested_audio_index,
                                       saw_fragment, &candidate,
                                       &candidate_track_id,
                                       &is_audio);
            if (rc != QW_STATUS_OK) {
                if (report && rc == QW_STATUS_ERR_UNSUPPORTED) {
                    (void)snprintf(report->note, sizeof(report->note),
                                   "native MP4 parser supports AAC/ALAC progressive sample tables and unencrypted fragmented tracks without edit lists");
                }
                return rc;
            }
            if (is_audio) {
                if (audio_track_count == requested_audio_index) {
                    *source = candidate;
                    selected_track_id = candidate_track_id;
                }
                ++audio_track_count;
            }
        }
        trak_position = child.next_offset;
    }
    if (!audio_track_count || requested_audio_index >= audio_track_count)
        return QW_STATUS_ERR_UNSUPPORTED;
    if (saw_fragment) {
        rc = qw_audio_mp4_parse_fragments(
            bytes, size, selected_track_id, source->sample_description_count,
            &source->fragment_index);
        if (rc != QW_STATUS_OK) {
            qw_audio_mp4_source_dispose(source);
            if (report && rc == QW_STATUS_ERR_UNSUPPORTED) {
                (void)snprintf(report->note, sizeof(report->note),
                               "native MP4 fragment parser requires clear AAC/ALAC samples with valid tfhd, tfdt, trun and mdat offsets");
            }
            return rc;
        }
        source->sample_count = source->fragment_index.sample_count;
        source->decoded_sample_count = source->fragment_index.decoded_duration;
        source->presentation_sample_count =
            source->fragment_index.decoded_duration;
        source->total_sample_bytes = source->fragment_index.total_sample_bytes;
    }
    source->data = bytes;
    source->size = size;
    source->audio_track_count = audio_track_count;
    source->has_metadata = 1;
    return QW_STATUS_OK;
}

void qw_audio_mp4_source_dispose(QwAudioMp4Source *source)
{
    if (!source) return;
    qw_audio_mp4_fragment_index_dispose(&source->fragment_index);
    memset(source, 0, sizeof(*source));
}

static uint32_t mp4_sample_size(const QwAudioMp4Source *source,
                                size_t sample_index)
{
    return mp4_sample_size_at(source, sample_index);
}

static uint64_t mp4_sample_chunk_offset(const QwAudioMp4Source *source,
                                        size_t chunk_index)
{
    const uint8_t *entry = source->data + source->chunk_entries_offset +
        chunk_index * source->chunk_offset_size;
    return source->chunk_offset_size == 8u
        ? qw_read_u64_be(entry) : qw_read_u32_be(entry);
}

int qw_audio_mp4_next_sample(const QwAudioMp4Source *source,
                             QwAudioMp4Cursor *cursor,
                             QwAudioMp4Sample *sample)
{
    uint32_t sample_size;
    uint32_t duration;
    int64_t composition_pts;
    int64_t composition_offset = 0;
    if (!source || !cursor || !sample) return QW_STATUS_ERR_INVALID_ARGUMENT;
    if (source->is_fragmented) {
        const QwAudioMp4FragmentSample *fragment_sample;
        if (cursor->sample_index >= source->fragment_index.sample_count)
            return 0;
        fragment_sample =
            &source->fragment_index.samples[cursor->sample_index];
        if (fragment_sample->offset > source->size ||
            fragment_sample->size > source->size - fragment_sample->offset) {
            return QW_STATUS_ERR_FORMAT;
        }
        sample->data = source->data + fragment_sample->offset;
        sample->size = fragment_sample->size;
        sample->pts = fragment_sample->pts;
        sample->no_pts = fragment_sample->no_pts;
        sample->duration = fragment_sample->duration;
        if (cursor->next_pts > UINT64_MAX - fragment_sample->duration)
            return QW_STATUS_ERR_FORMAT;
        cursor->next_pts += fragment_sample->duration;
        ++cursor->sample_index;
        return 1;
    }
    if (cursor->sample_index >= source->sample_count) return 0;

    if (!cursor->chunk_samples_remaining) {
        const uint8_t *mapping;
        uint32_t first_chunk;
        uint32_t samples_per_chunk;
        uint64_t chunk_offset;
        if (cursor->chunk_index >= source->chunk_count)
            return QW_STATUS_ERR_FORMAT;
        while (cursor->stsc_index + 1u < source->stsc_entry_count &&
               qw_read_u32_be(source->data + source->stsc_entries_offset +
                              (cursor->stsc_index + 1u) * 12u) <=
                   cursor->chunk_index + 1u) {
            ++cursor->stsc_index;
        }
        mapping = source->data + source->stsc_entries_offset +
                  cursor->stsc_index * 12u;
        first_chunk = qw_read_u32_be(mapping);
        samples_per_chunk = qw_read_u32_be(mapping + 4u);
        if (first_chunk > cursor->chunk_index + 1u ||
            !samples_per_chunk ||
            samples_per_chunk > source->sample_count - cursor->sample_index) {
            return QW_STATUS_ERR_FORMAT;
        }
        chunk_offset = mp4_sample_chunk_offset(source, cursor->chunk_index);
        if (chunk_offset > (uint64_t)SIZE_MAX) return QW_STATUS_ERR_FORMAT;
        cursor->chunk_data_offset = (size_t)chunk_offset;
        cursor->chunk_samples_remaining = samples_per_chunk;
        ++cursor->chunk_index;
    }

    sample_size = mp4_sample_size(source, cursor->sample_index);
    if (!sample_size || sample_size > (uint32_t)INT_MAX ||
        cursor->chunk_data_offset > source->size ||
        sample_size > source->size - cursor->chunk_data_offset) {
        return QW_STATUS_ERR_FORMAT;
    }
    if (!cursor->stts_samples_remaining) {
        const uint8_t *timing;
        if (cursor->stts_index >= source->stts_entry_count)
            return QW_STATUS_ERR_FORMAT;
        timing = source->data + source->stts_entries_offset +
                 cursor->stts_index * 8u;
        cursor->stts_samples_remaining = qw_read_u32_be(timing);
        cursor->stts_sample_duration = qw_read_u32_be(timing + 4u);
        ++cursor->stts_index;
    }
    duration = cursor->stts_sample_duration;
    if (!duration || cursor->next_pts > UINT64_MAX - duration)
        return QW_STATUS_ERR_FORMAT;
    if (source->has_composition_offsets) {
        int rc = qw_audio_mp4_composition_next_offset(
            &source->composition_table, &cursor->composition_cursor,
            &composition_offset);
        if (rc != QW_STATUS_OK) return rc;
    }
    if (cursor->next_pts > (uint64_t)INT64_MAX ||
        (composition_offset > 0 &&
         cursor->next_pts >
             (uint64_t)(INT64_MAX - composition_offset))) {
        return QW_STATUS_ERR_FORMAT;
    }
    composition_pts = (int64_t)cursor->next_pts +
                      composition_offset;
    sample->data = source->data + cursor->chunk_data_offset;
    sample->size = sample_size;
    sample->no_pts = composition_pts < 0;
    sample->pts = composition_pts < 0 ? 0u : (uint64_t)composition_pts;
    sample->duration = duration;
    cursor->chunk_data_offset += sample_size;
    --cursor->chunk_samples_remaining;
    --cursor->stts_samples_remaining;
    ++cursor->sample_index;
    cursor->next_pts += duration;
    return 1;
}
