// ============================================================
// Microgroove [BRANCH: live-sampling] - mic_sampler.h
// Live sampling from the ES8311/MEMS mic + engine resampling.
//   HOLD AUX (.) 0.5s        -> mic records to the current drum
//                               lane while AUX stays held;
//                               release = auto-trim + commit
//   HOLD SONG (n) 0.5s while -> resample ~1.9s of the mix,
//   playing                     then tap any pad to commit
// Committed samples are written to SD as WAV so projects reload
// them by name like any other sample. RAM-only fallback if no SD.
// ============================================================
#pragma once
#include "config.h"
#include "sampler_slots.h"

#define MIC_RATE          16000        // capture rate (stored native, voice retunes)
#define SCRATCH_FRAMES    42000        // ~2.6s mic / ~1.9s resample @22.05k
#define TRIM_THRESHOLD    600          // |sample| gate for auto-trim
#define TRIM_PREROLL_MS   30

bool micSamplerInit();                 // alloc scratch - call BEFORE samplerInit()
void micSamplerUpdate();               // pump capture; call every loop()

bool micRecStart(uint8_t lane);        // pauses transport+speaker, starts capture
bool micStreamRecStart(uint8_t slot, SamplerSlotMode mode);
void micRecStop();                     // trim, commit, assign lane, resume audio
bool micRecActive();

void resampleArm();                    // audio task fills scratch with the mix
bool resamplePending();                // captured, waiting for a destination pad
void resampleCommit(uint8_t lane);

// audio_engine tap (single writer: core 0)
extern int16_t*          g_scratch;
extern volatile uint32_t g_rsmpRemain;
extern volatile uint32_t g_scratchWr;
