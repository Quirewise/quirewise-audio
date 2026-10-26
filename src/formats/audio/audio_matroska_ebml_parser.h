#ifndef QUIREWISE_AUDIO_MATROSKA_EBML_PARSER_H
#define QUIREWISE_AUDIO_MATROSKA_EBML_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMatroskaEbmlElement {
    uint32_t id;
    size_t payload;
    size_t size;
    size_t next;
    int unknown_size;
} QwAudioMatroskaEbmlElement;

int qw_audio_matroska_ebml_read_vint(const uint8_t *data,
                                    size_t size,
                                    size_t *position,
                                    int keep_marker,
                                    uint64_t *value,
                                    unsigned *length_out);

int qw_audio_matroska_ebml_read_element(
        const uint8_t *data,
        size_t parent_end,
        size_t position,
        QwAudioMatroskaEbmlElement *element);

int qw_audio_matroska_ebml_read_uint(const uint8_t *data,
                                    size_t size,
                                    uint64_t *value);

#endif
