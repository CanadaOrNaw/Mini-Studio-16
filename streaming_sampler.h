#pragma once

#include "sample_stream_core.h"
#include "sampler_slots.h"

#include <stdint.h>

static const uint8_t STREAMING_SAMPLE_VOICES = 4;

enum StreamingSamplerInput : uint8_t {
    STREAM_SAMPLE_INPUT_NONE = 0,
    STREAM_SAMPLE_INPUT_BUS,
    STREAM_SAMPLE_INPUT_MIC,
};

enum StreamingSamplerRecordState : uint8_t {
    STREAM_SAMPLE_REC_IDLE = 0,
    STREAM_SAMPLE_REC_STARTING,
    STREAM_SAMPLE_REC_RECORDING,
    STREAM_SAMPLE_REC_STOPPING,
    STREAM_SAMPLE_REC_COMPLETE,
    STREAM_SAMPLE_REC_ERROR,
};

struct StreamingSamplerSnapshot {
    bool available;
    uint32_t queuedCommands;
    uint32_t commandDrops;
    uint32_t starts;
    uint32_t errors;
    uint32_t maxReadUs;
    StreamingSamplerRecordState recordState;
    StreamingSamplerInput recordInput;
    uint8_t recordSlot;
    uint32_t recordFrames;
    uint32_t recordTargetFrames;
    uint32_t recordDroppedFrames;
    SampleStreamVoiceSnapshot voices[STREAMING_SAMPLE_VOICES];
};

void streamingSamplerInit(bool sdMounted);
int32_t streamingSamplerRender();
bool streamingSamplerTrigger(uint8_t slot, uint8_t key,
                             const SamplerLockEntry* lock = nullptr);
bool streamingSamplerAssign(uint8_t slot, const char* filename,
                            SamplerSlotMode mode);
bool streamingSamplerClear(uint8_t slot);
bool streamingSamplerBeginRecord(uint8_t slot, SamplerSlotMode mode,
                                 uint32_t sourceRate, StreamingSamplerInput input);
size_t streamingSamplerRecordPush(StreamingSamplerInput input,
                                  const int16_t* frames, size_t count);
bool streamingSamplerStopRecord();
void streamingSamplerStopAll();
bool streamingSamplerBusy();
bool streamingSamplerIsRecording();
StreamingSamplerSnapshot streamingSamplerSnapshot();
const char* sampleStreamStateName(SampleStreamState state);
