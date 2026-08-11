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
AudioDspSnapshot audioEngineDspSnapshot();
void audioEngineResetDspStats();
