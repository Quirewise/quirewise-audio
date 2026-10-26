#ifndef QUIREWISE_AUDIO_MP4_EDIT_PARSER_H
#define QUIREWISE_AUDIO_MP4_EDIT_PARSER_H

#include <stddef.h>
#include <stdint.h>

int qw_audio_mp4_parse_edit_list(const uint8_t *payload,
                                size_t payload_size,
                                uint32_t movie_timescale,
                                uint32_t media_timescale,
                                uint32_t sample_rate,
                                uint64_t media_sample_count,
                                int64_t composition_offset,
                                uint64_t *out_trim_start_samples,
                                uint64_t *out_presentation_sample_count,
                                uint64_t *out_leading_silence_samples);

#endif
