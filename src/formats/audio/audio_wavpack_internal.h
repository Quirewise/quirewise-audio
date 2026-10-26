#ifndef QUIREWISE_AUDIO_WAVPACK_INTERNAL_H
#define QUIREWISE_AUDIO_WAVPACK_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#define AUDIO_WAVPACK_MONO_FLAG UINT32_C(0x00000004)
#define AUDIO_WAVPACK_HYBRID_FLAG UINT32_C(0x00000008)
#define AUDIO_WAVPACK_DSD_FLAG UINT32_C(0x80000000)
#define AUDIO_WAVPACK_FALSE_STEREO_FLAG UINT32_C(0x40000000)
#define AUDIO_WAVPACK_INITIAL_FLAG UINT32_C(0x00000800)
#define AUDIO_WAVPACK_FINAL_FLAG UINT32_C(0x00001000)
#define AUDIO_WAVPACK_METADATA_LONG_FLAG UINT8_C(0x80)
#define AUDIO_WAVPACK_METADATA_ODD_FLAG UINT8_C(0x40)
#define AUDIO_WAVPACK_METADATA_ID_MASK UINT8_C(0x3f)
#define AUDIO_WAVPACK_METADATA_SAMPLE_RATE UINT8_C(0x27)
#define AUDIO_WAVPACK_METADATA_DSD_DATA UINT8_C(0x0e)

extern const uint32_t audio_wavpack_sample_rates[16];

int audio_wavpack_parse_custom_sample_rate(const uint8_t *data,
                                           size_t size,
                                           uint32_t *sample_rate);
int audio_wavpack_parse_dsd_rate_factor(const uint8_t *data,
                                        size_t size,
                                        uint32_t *rate_factor);

#endif
