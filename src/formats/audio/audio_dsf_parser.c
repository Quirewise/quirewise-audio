#include "audio_dsf_parser.h"

#include "../../core/byte_utils.h"
#include "quirewise/status.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

enum {
    DSF_ROOT_CHUNK_SIZE = 28u,
    DSF_FORMAT_CHUNK_OFFSET = 28u,
    DSF_FORMAT_CHUNK_SIZE = 52u,
    DSF_DATA_CHUNK_OFFSET = 80u,
    DSF_DATA_HEADER_SIZE = 12u,
    DSF_AUDIO_DATA_OFFSET = DSF_DATA_CHUNK_OFFSET + DSF_DATA_HEADER_SIZE,
    DSF_MINIMUM_SIZE = DSF_AUDIO_DATA_OFFSET
};

static uint16_t dsf_channel_count(uint32_t channel_type)
{
    static const uint8_t channel_counts[] = {0u, 1u, 2u, 3u, 4u, 4u, 5u, 6u};
    if (channel_type >= sizeof(channel_counts)) return 0u;
    return channel_counts[channel_type];
}

static int dsf_note_unsupported(QwConvertReport *report, const char *message)
{
    if (report) {
        (void)snprintf(report->note, sizeof(report->note), "%s", message);
    }
    return QW_STATUS_ERR_UNSUPPORTED;
}

int qw_audio_dsf_parse(const uint8_t *bytes,
                       size_t size,
                       QwAudioDsfSource *source,
                       QwConvertReport *report)
{
    uint64_t declared_size;
    uint64_t metadata_offset;
    uint64_t data_chunk_size;
    uint32_t sample_frequency;
    uint64_t audio_bytes_per_channel;
    size_t file_size;
    size_t data_end;
    size_t packed_data_size;
    size_t padded_bytes_per_channel;
    size_t padded_data_size;
    size_t block_count;
    size_t block_size;
    uint32_t format_version;
    uint32_t format_id;
    uint32_t channels;
    uint32_t bits_per_sample;

    if (!bytes || !source) return QW_STATUS_ERR_INVALID_ARGUMENT;
    memset(source, 0, sizeof(*source));
    if (size < DSF_MINIMUM_SIZE || memcmp(bytes, "DSD ", 4u) != 0)
        return QW_STATUS_ERR_FORMAT;
    if (qw_read_u64_le(bytes + 4u) != DSF_ROOT_CHUNK_SIZE)
        return QW_STATUS_ERR_FORMAT;

    declared_size = qw_read_u64_le(bytes + 12u);
    metadata_offset = qw_read_u64_le(bytes + 20u);
    if (declared_size < DSF_MINIMUM_SIZE || declared_size > size) {
        return QW_STATUS_ERR_FORMAT;
    }
    file_size = (size_t)declared_size;
    if ((uint64_t)file_size != declared_size) return QW_STATUS_ERR_FORMAT;

    if (memcmp(bytes + DSF_FORMAT_CHUNK_OFFSET, "fmt ", 4u) != 0 ||
        qw_read_u64_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 4u) !=
            DSF_FORMAT_CHUNK_SIZE) {
        return QW_STATUS_ERR_FORMAT;
    }
    format_version = qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 12u);
    format_id = qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 16u);
    if (format_version != 1u || format_id != 0u) {
        return dsf_note_unsupported(report,
                                    "native DSF parser supports version 1 DSD raw data");
    }

    source->channel_type =
        qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 20u);
    channels = qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 24u);
    if (!channels || channels > UINT16_MAX ||
        channels != dsf_channel_count(source->channel_type)) {
        return QW_STATUS_ERR_FORMAT;
    }
    sample_frequency =
        qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 28u);
    bits_per_sample =
        qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 32u);
    if (bits_per_sample != 1u && bits_per_sample != 8u) {
        return dsf_note_unsupported(report,
                                    "native DSF parser supports 1-bit and 8-bit DSD data");
    }
    source->bits_per_sample = (uint8_t)bits_per_sample;
    if (sample_frequency < 8u || sample_frequency % 8u != 0u) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->sample_rate = sample_frequency / 8u;
    source->sample_count =
        qw_read_u64_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 36u);
    if (!source->sample_count) return QW_STATUS_ERR_FORMAT;
    block_size = qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 44u);
    if (!block_size) return QW_STATUS_ERR_FORMAT;
    if ((uint64_t)block_size * channels > (uint64_t)INT_MAX) {
        return dsf_note_unsupported(report,
                                    "native DSF parser block size exceeds decoder packet limits");
    }
    if (qw_read_u32_le(bytes + DSF_FORMAT_CHUNK_OFFSET + 48u) != 0u)
        return QW_STATUS_ERR_FORMAT;
    source->channels = (uint16_t)channels;

    data_chunk_size = qw_read_u64_le(bytes + DSF_DATA_CHUNK_OFFSET + 4u);
    if (memcmp(bytes + DSF_DATA_CHUNK_OFFSET, "data", 4u) != 0 ||
        data_chunk_size < DSF_DATA_HEADER_SIZE ||
        data_chunk_size > file_size - DSF_DATA_CHUNK_OFFSET) {
        return QW_STATUS_ERR_FORMAT;
    }
    data_end = DSF_DATA_CHUNK_OFFSET + (size_t)data_chunk_size;
    if (metadata_offset &&
        (metadata_offset < data_end || metadata_offset >= file_size)) {
        return QW_STATUS_ERR_FORMAT;
    }

    audio_bytes_per_channel = source->sample_count / 8u;
    if (source->sample_count % 8u) ++audio_bytes_per_channel;
    if (!audio_bytes_per_channel) return QW_STATUS_ERR_FORMAT;
    source->audio_bytes_per_channel = (size_t)audio_bytes_per_channel;
    if ((uint64_t)source->audio_bytes_per_channel != audio_bytes_per_channel)
        return QW_STATUS_ERR_FORMAT;
    if (!qw_checked_size_mul((size_t)audio_bytes_per_channel, channels,
                             &packed_data_size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    block_size = (size_t)block_size;
    block_count = source->audio_bytes_per_channel / block_size;
    if (source->audio_bytes_per_channel % block_size) ++block_count;
    if (!qw_checked_size_mul(block_count, block_size,
                             &padded_bytes_per_channel) ||
        !qw_checked_size_mul(padded_bytes_per_channel, channels,
                             &padded_data_size)) {
        return QW_STATUS_ERR_FORMAT;
    }
    source->data_size = (size_t)data_chunk_size - DSF_DATA_HEADER_SIZE;
    if (source->data_size != packed_data_size &&
        source->data_size != padded_data_size) {
        return QW_STATUS_ERR_FORMAT;
    }

    source->data = bytes;
    source->size = file_size;
    source->data_offset = DSF_AUDIO_DATA_OFFSET;
    source->block_size = block_size;
    source->block_count = block_count;
    source->has_padded_last_block =
        source->data_size == padded_data_size &&
        source->audio_bytes_per_channel % source->block_size != 0u;
    source->has_metadata = metadata_offset != 0u;
    return QW_STATUS_OK;
}
