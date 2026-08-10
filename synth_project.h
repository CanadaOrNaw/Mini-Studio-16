#pragma once

#include "synth_engine.h"

#include <stdint.h>

struct __attribute__((packed)) SaveSynthAdsr {
    uint16_t attackMs;
    uint16_t decayMs;
    uint16_t releaseMs;
    uint16_t sustainQ15;
};

struct __attribute__((packed)) SaveMgPlusPatch {
    uint8_t oscMode;
    uint8_t wavetable;
    uint8_t filterMode;
    uint8_t lfoDestination;
    uint16_t cutoffQ15;
    uint16_t resonanceQ15;
    uint16_t filterEnvQ15;
    uint16_t pulseWidthQ15;
    uint16_t subLevelQ15;
    uint16_t lfoRateCent;
    uint16_t lfoDepthQ15;
    uint16_t velocityAmpQ15;
    uint16_t velocityFilterQ15;
    uint16_t driveQ15;
    uint16_t volumeQ15;
    SaveSynthAdsr ampEnvelope;
    SaveSynthAdsr filterEnvelope;
};

struct __attribute__((packed)) SaveFmOperatorPatch {
    uint16_t ratioCent;
    uint16_t levelQ15;
    SaveSynthAdsr envelope;
};

struct __attribute__((packed)) SaveFmPatch {
    uint8_t algorithm;
    uint16_t feedbackQ15;
    uint16_t modulationIndexCent;
    uint16_t volumeQ15;
    SaveFmOperatorPatch operators[4];
};

struct __attribute__((packed)) SaveSynthEngineState {
    uint8_t engine;
    SaveMgPlusPatch mgx;
    SaveFmPatch fm;
};

static_assert(sizeof(SaveSynthAdsr) == 8, "synth ADSR storage layout changed");
static_assert(sizeof(SaveMgPlusPatch) == 42, "MGX storage layout changed");
static_assert(sizeof(SaveFmOperatorPatch) == 12, "FM operator storage layout changed");
static_assert(sizeof(SaveFmPatch) == 55, "FM storage layout changed");
static_assert(sizeof(SaveSynthEngineState) == 98, "synth engine storage layout changed");

void synthProjectEncode(const SynthTrack& track, SaveSynthEngineState& output);
bool synthProjectValidate(const SaveSynthEngineState& input);
bool synthProjectDecode(const SaveSynthEngineState& input, SynthTrack& track);
void synthProjectMigrateLegacy(SynthTrack& track);

