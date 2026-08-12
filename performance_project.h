#pragma once

#include "performance_state.h"

struct __attribute__((packed)) SaveChordSettings {
    uint8_t key, scale, map;
    int8_t octave;
    uint8_t bassMode, voiceLeading;
    int8_t octaveShift[7], inversion[7];
    uint8_t lockedType[7];
};
struct __attribute__((packed)) SaveMedoTrackSettings {
    uint8_t volume;
    int8_t octave;
    uint8_t quantize;
};
struct __attribute__((packed)) SavePerformancePreset {
    uint8_t valid;
    SaveChordSettings chord;
    SaveSynthEngineState synths[3];
};
struct __attribute__((packed)) SavePerformanceState {
    SaveChordSettings chord;
    uint8_t hiChordMode;
    HiChordSequenceStep chordSequence[16];
    uint8_t poEffects[16][16];
    uint8_t medoRole;
    SaveMedoTrackSettings medoTracks[MEDO_ROLE_COUNT];
    MasterEffectsSettings masterEffects;
    VocoderSettings vocoder;
    uint8_t swing;
    uint8_t hiChordArpPattern, hiChordArpLayer, hiChordArpRate, hiChordRepeatRate;
    uint8_t hiChordDrumKit, hiChordGrooveStyle, hiChordGrooveVariation;
    uint8_t hiChordPracticeSong, hiChordPracticePosition, hiChordEarLevel;
    uint16_t hiChordEarScore;
    uint8_t medoScale, medoArpDirection, medoArpRate, medoSharedBars;
    SavePerformancePreset presets[4];
};

void performanceProjectEncode(SavePerformanceState &out);
bool performanceProjectValidate(const SavePerformanceState &in);
bool performanceProjectDecode(const SavePerformanceState &in);

static_assert(sizeof(SaveChordSettings) == 27, "chord settings storage changed");
static_assert(sizeof(SavePerformanceState) == 1688, "performance storage changed");
