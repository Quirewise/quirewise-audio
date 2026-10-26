#ifndef QUIREWISE_AUDIO_ID3_TAG_H
#define QUIREWISE_AUDIO_ID3_TAG_H

#include <stddef.h>
#include <stdint.h>

/* Returns 1 with the leading ID3v2 byte count, or 0 for a malformed tag. */
int qw_audio_id3_skip_leading_v2(const uint8_t *bytes,
                                 size_t size,
                                 size_t *skip_bytes);

/* Returns the audio-data end and reports whether a trailing ID3v1 tag exists. */
size_t qw_audio_id3v1_audio_end(const uint8_t *bytes,
                                size_t size,
                                int *has_tag);

#endif
