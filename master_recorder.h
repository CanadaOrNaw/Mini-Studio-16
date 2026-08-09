#pragma once

#include <stddef.h>
#include <stdint.h>

enum MasterRecorderState : uint8_t {
    MASTER_REC_UNAVAILABLE = 0,
    MASTER_REC_IDLE,
    MASTER_REC_STARTING,
    MASTER_REC_RECORDING,
    MASTER_REC_STOPPING,
    MASTER_REC_COMPLETE,
    MASTER_REC_ERROR,
};

struct MasterRecorderSnapshot {
    MasterRecorderState state;
    char path[64];
    uint32_t framesWritten;
    uint32_t droppedFrames;
    uint32_t ringHighWater;
    uint32_t maxWriteUs;
    uint32_t errors;
    char recoveredPath[64];
    uint32_t recoveredFrames;
};

void masterRecorderInit(bool sdMounted);
bool masterRecorderStart();
bool masterRecorderStop();
bool masterRecorderIsBusy();
void masterRecorderPush(const int16_t* frames, size_t count);
MasterRecorderSnapshot masterRecorderSnapshot();
const char* masterRecorderStateName(MasterRecorderState state);
