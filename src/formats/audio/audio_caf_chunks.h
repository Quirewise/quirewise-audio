#ifndef QUIREWISE_AUDIO_CAF_CHUNKS_H
#define QUIREWISE_AUDIO_CAF_CHUNKS_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioCafChunks {
    const uint8_t *description;
    size_t description_size;
    const uint8_t *audio_data;
    size_t audio_data_size;
    const uint8_t *packet_table;
    size_t packet_table_size;
    const uint8_t *magic_cookie;
    size_t magic_cookie_size;
    const uint8_t *channel_layout;
    size_t channel_layout_size;
    uint32_t data_edit_count;
    int has_metadata;
} QwAudioCafChunks;

int qw_audio_caf_parse_chunks(const uint8_t *bytes,
                              size_t size,
                              QwAudioCafChunks *chunks);

#endif
