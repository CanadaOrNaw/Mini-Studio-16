// ============================================================
// CardputerGroovebox - audio_engine.h
// ============================================================
#pragma once
#include "config.h"

extern float g_scopeBuf[SCREEN_W];
extern volatile int g_scopeIdx;

struct AudioDspSnapshot {
    uint32_t blocks;
    uint32_t lastRenderUs;
    uint32_t maxRenderUs;
    uint32_t deadlineMisses;
    uint32_t deadlineUs;
};

void audioEngineStart();   // creates the render task on core 0
// Project loading performs several multi-word resets that cannot race core-0
// rendering. Begin waits for an acknowledged audio block boundary; End lets
// rendering resume. A timeout fails closed instead of mutating live DSP state.
bool audioEngineBeginExclusiveMutation(uint32_t timeoutMs);
void audioEngineEndExclusiveMutation();
AudioDspSnapshot audioEngineDspSnapshot();
void audioEngineResetDspStats();
