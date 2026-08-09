#pragma once

#include "stem_file.h"

#include <stddef.h>
#include <stdint.h>

enum StemRecorderState : uint8_t {
    STEM_REC_UNAVAILABLE = 0,
    STEM_REC_IDLE,
    STEM_REC_STARTING,
    STEM_REC_RECORDING,
    STEM_REC_STOPPING,
    STEM_REC_COMPLETE,
    STEM_REC_ERROR,
};

struct StemRecorderSnapshot {
    StemRecorderState state;
    char path[64];
    uint32_t framesWritten;
    uint32_t droppedFrames;
    uint32_t ringHighWater;
    uint32_t maxWriteUs;
    uint32_t errors;
};

void stemRecorderInit(bool sdMounted);
bool stemRecorderStart();
bool stemRecorderStop();
bool stemRecorderIsBusy();
void stemRecorderPush(const StemPcmFrame* frames, size_t count);
StemRecorderSnapshot stemRecorderSnapshot();
const char* stemRecorderStateName(StemRecorderState state);
