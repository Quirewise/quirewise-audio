#ifndef QUIREWISE_AUDIO_MATROSKA_ADDITION_MAPPING_PARSER_H
#define QUIREWISE_AUDIO_MATROSKA_ADDITION_MAPPING_PARSER_H

#include <stddef.h>
#include <stdint.h>

typedef struct QwAudioMatroskaAdditionMapping {
    uint64_t block_add_id_type;
    uint64_t block_add_id_value;
    int has_block_add_id_value;
} QwAudioMatroskaAdditionMapping;

/* Parses one BlockAdditionMapping payload. */
int qw_audio_matroska_addition_mapping_parse(const uint8_t *data,
                                            size_t size,
                                            QwAudioMatroskaAdditionMapping *mapping);

#endif
