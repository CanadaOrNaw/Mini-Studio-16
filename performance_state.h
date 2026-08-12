#pragma once

#include "chord_engine.h"
#include "hichord_performance.h"
#include "medo_performance.h"
#include "po_effects.h"
#include "synth_project.h"
#include "master_effects.h"
#include "vocoder.h"

extern ChordEngine g_chordEngine;
extern ChordSettings g_chordSettings;
extern HiChordPerformance g_hiChordPerformance;
extern MedoPerformance g_medoPerformance;
extern PoPatternEffects g_poPatternEffects;
extern PoEffectProcessor g_poEffectProcessor;
extern MasterEffects g_masterEffects;
extern Vocoder8Band g_vocoder;

extern uint8_t g_chordParameter;
extern uint8_t g_chordDegree;
extern uint8_t g_poEffectSelection;
extern bool g_poLiveEffectActive;
extern uint8_t g_medoParameter;
extern uint8_t g_masterEffectSelection;
extern uint8_t g_masterEffectParameter;
extern uint8_t g_vocoderParameter;
extern bool g_hiChordBounceActive;
extern int8_t g_hiChordBounceTrack;
extern uint8_t g_hiChordDrumKit;
extern uint8_t g_hiChordGrooveStyle;
extern uint8_t g_hiChordGrooveVariation;
extern uint8_t g_hiChordPracticeSong;
extern uint8_t g_hiChordPracticePosition;
extern uint8_t g_hiChordEarLevel;
extern uint8_t g_hiChordEarTarget;
extern uint16_t g_hiChordEarScore;
extern uint8_t g_hiChordArpPattern;
extern uint8_t g_hiChordArpLayer;
extern uint8_t g_hiChordArpRate;
extern uint8_t g_hiChordRepeatRate;

void performanceStateInit();
bool performanceStorePreset(uint8_t index);
bool performanceRecallPreset(uint8_t index);
bool performancePresetValid(uint8_t index);

struct PerformancePreset {
    uint8_t valid;
    ChordSettings chord;
    SaveSynthEngineState synths[3];
};
extern PerformancePreset g_performancePresets[4];
