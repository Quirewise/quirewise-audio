#ifndef QUIREWISE_AUDIO_CUSTOM_SAMPLE_PLAN_H
#define QUIREWISE_AUDIO_CUSTOM_SAMPLE_PLAN_H

#include "audio_custom_input.h"

#include <stdint.h>

typedef struct QwAudioCustomSamplePlan {
    uint64_t skip_samples;
    uint64_t leading_silence_samples;
    uint64_t sample_limit;
    int limit_enabled;
} QwAudioCustomSamplePlan;

void qw_audio_custom_sample_plan_build(const QwAudioCustomInput *input,
                                       QwAudioCustomSamplePlan *plan);

#endif