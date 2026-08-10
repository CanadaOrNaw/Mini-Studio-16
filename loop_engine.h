#pragma once

#include "loop_stream_core.h"

#include <stdint.h>

struct LoopEngineSnapshot {
    bool available;
    uint8_t recordTrack;
    uint32_t timelineFrames;
    uint32_t absoluteFrame;
    uint32_t maxReadUs;
    uint32_t maxWriteUs;
    uint32_t errors;
    LoopStreamTrackSnapshot tracks[LOOP_STREAM_TRACKS];
};

void loopEngineInit(bool sdMounted);
int32_t loopEngineProcessFrame(int16_t dryInput);
bool loopEngineRequestRecord(uint8_t track);
bool loopEngineStopRecording(uint8_t track);
bool loopEngineSetMuted(uint8_t track, bool muted);
bool loopEngineClear(uint8_t track);
bool loopEngineIsRecording();
bool loopEngineHasActiveIo();
LoopEngineSnapshot loopEngineSnapshot();
const char* loopEngineStateName(LoopStreamState state);
