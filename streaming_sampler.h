#pragma once

#include "sample_stream_core.h"
#include "sampler_slots.h"

#include <stdint.h>

static const uint8_t STREAMING_SAMPLE_VOICES = 4;

struct StreamingSamplerSnapshot {
    bool available;
    uint32_t queuedCommands;
    uint32_t commandDrops;
    uint32_t starts;
    uint32_t errors;
    uint32_t maxReadUs;
    SampleStreamVoiceSnapshot voices[STREAMING_SAMPLE_VOICES];
};

void streamingSamplerInit(bool sdMounted);
int32_t streamingSamplerRender();
bool streamingSamplerTrigger(uint8_t slot, uint8_t key,
                             const SamplerLockEntry* lock = nullptr);
bool streamingSamplerAssign(uint8_t slot, const char* filename,
                            SamplerSlotMode mode);
bool streamingSamplerClear(uint8_t slot);
void streamingSamplerStopAll();
bool streamingSamplerBusy();
StreamingSamplerSnapshot streamingSamplerSnapshot();
const char* sampleStreamStateName(SampleStreamState state);

