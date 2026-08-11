// ============================================================
// Microgroove [BRANCH: live-sampling] - mic_sampler.h
// Live sampling from the ES8311/MEMS mic + engine resampling.
//   HOLD AUX (.) 0.5s        -> mic records to the current drum
//                               lane while AUX stays held;
//                               release = auto-trim + commit
//   HOLD SONG (n) 0.5s while -> resample ~1.9s of the mix,
//   playing                     then tap any pad to commit
// Committed samples are written through the bounded SD sampler recorder and
// remain project-reloadable.  No whole-take scratch allocation is required.
// ============================================================
#pragma once
#include "config.h"
#include "sampler_slots.h"

#define MIC_RATE          16000        // capture rate (stored native, voice retunes)
#define SCRATCH_FRAMES    42000        // ~2.6s mic / ~1.9s resample @22.05k
#define MIC_CAPTURE_CHUNK 256

bool micSamplerInit();                 // initialize the streamed legacy bridge
void micSamplerUpdate();               // pump capture; call every loop()

bool micRecStart(uint8_t lane);        // pauses transport+speaker, starts capture
bool micStreamRecStart(uint8_t slot, SamplerSlotMode mode);
void micRecStop();                     // finalize, assign lane, resume audio
bool micRecActive();
bool micSamplerHasPendingCommit();

void resampleArm();                    // bounded sampler recorder captures the mix
bool resamplePending();                // captured, waiting for a destination pad
void resampleCommit(uint8_t lane);
