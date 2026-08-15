#pragma once

#include "config.h"
#include "synth_dsp.h"
#include "synth_voice.h"

#include <stdint.h>

enum SynthEngine : uint8_t {
    SYNTH_ENGINE_MG = 0,
    SYNTH_ENGINE_MGX,
    SYNTH_ENGINE_FM4,
    SYNTH_ENGINE_COUNT,
};

enum SynthFilterMode : uint8_t {
    SYNTH_FILTER_LP = 0,
    SYNTH_FILTER_BP,
    SYNTH_FILTER_HP,
    SYNTH_FILTER_COUNT,
};

enum SynthLfoDestination : uint8_t {
    SYNTH_LFO_NONE = 0,
    SYNTH_LFO_PITCH,
    SYNTH_LFO_FILTER,
    SYNTH_LFO_PWM,
    SYNTH_LFO_AMP,
    SYNTH_LFO_COUNT,
};

struct MgPlusPatch {
    OscMode oscMode;
    uint8_t wavetable;
    SynthFilterMode filterMode;
    SynthLfoDestination lfoDestination;
    float cutoff;
    float resonance;
    float filterEnvAmount;
    float pulseWidth;
    float subLevel;
    float lfoRate;
    float lfoDepth;
    float velocityAmp;
    float velocityFilter;
    float drive;
    float volume;
    SynthAdsrParams ampEnvelope;
    SynthAdsrParams filterEnvelope;

    void init();
};

struct MgPlusVoice {
    uint32_t phase;
    uint32_t subPhase;
    uint32_t lfoPhase;
    float frequency;
    float targetFrequency;
    float filterLP;
    float filterBP;
    float velocity;
    uint8_t note;
    bool accent;
    bool slide;
    bool active;
    SynthAdsr ampEnvelope;
    SynthAdsr filterEnvelope;

    void init();
    void noteOn(float newFrequency, uint8_t midiNote, uint8_t midiVelocity,
                bool accented, bool legato, const MgPlusPatch& patch);
    void noteOff(const MgPlusPatch& patch);
    float render(const MgPlusPatch& patch);
    float level() const { return ampEnvelope.value; }
};

struct FmOperatorPatch {
    float ratio;
    float level;
    SynthAdsrParams envelope;

    void init(float frequencyRatio, float outputLevel);
};

struct FmPatch {
    uint8_t algorithm;
    float feedback;
    float modulationIndex;
    float volume;
    FmOperatorPatch operators[4];

    void init();
};

struct FmOperatorState {
    uint32_t phase;
    uint32_t increment;
    SynthAdsr envelope;
};

struct FmVoice {
    FmOperatorState operators[4];
    float feedbackSample;
    float velocity;
    uint8_t note;
    bool active;

    void init();
    void noteOn(float frequency, uint8_t midiNote, uint8_t midiVelocity,
                const FmPatch& patch);
    void noteOff(const FmPatch& patch);
    float render(const FmPatch& patch);
    float level() const;
};

// Fixed-size per-track engine. The legacy `v[]` array and `forEach()` API are
// intentionally retained so old code and old projects continue to use the
// original SynthVoice implementation unchanged.
struct SynthTrack {
    SynthVoice v[MAX_POLY];
    MgPlusVoice mgxVoices[MAX_POLY];
    FmVoice fmVoices[MAX_POLY];
    MgPlusPatch mgxPatch;
    FmPatch fmPatch;
    SynthEngine engine;
    uint8_t voices;
    uint8_t rr;
    // P2-9 (reconciliation report): bit i marks voice i as started by a live
    // source (keyboard audition, MIDI in, serial `note`). The sequencer's
    // per-step housekeeping releases only sequencer-owned voices, so a note
    // a player is holding is never clipped by empty pattern steps. Any
    // release or reallocation of a voice clears its bit.
    uint8_t liveMask;
    // P2-8: engine switches requested from the UI/serial/storage side are
    // parked here and applied by the audio task at the next block boundary
    // (applyPendingEngine), so setEngine's multi-word voice re-init can
    // never interleave with an in-flight render() on the other core.
    // 0xFF = no request pending. Accessed only through __atomic builtins.
    uint8_t pendingEngine;
    // A2-P1-5 (alpha.2 reconciliation): hardStop() and setVoices() perform
    // the same multi-word voice re-initialisation that pendingEngine exists
    // to defer, but the HiChord Lead/Drone paths call them from the input
    // task on every chord change. Requests are parked here and consumed by
    // the audio task in applyPendingVoiceReset(). Bit 0 = hard stop
    // requested; bits 1..7 = requested voice count (0 = unchanged).
    uint8_t pendingVoiceOp;

    void init();

    template <typename F> void forEach(F f) {
        for (int i = 0; i < MAX_POLY; ++i) f(v[i]);
    }

    void setEngine(SynthEngine selected);
    // Cross-task-safe engine selection: request from any task, apply on the
    // audio task. displayEngine() lets UI/serial/save paths observe the
    // requested engine immediately instead of lagging one audio block.
    void requestEngine(SynthEngine selected);
    void applyPendingEngine();
    // Cross-task-safe equivalents of hardStop()/setVoices(): request from any
    // task, applied by the audio task at the next block boundary.
    void requestHardStop();
    void requestVoices(uint8_t count);
    void applyPendingVoiceReset();
    SynthEngine displayEngine() const;
    void setVoices(uint8_t count);
    void noteOn(float frequency, bool accent, bool slide);
    int noteOn(float frequency, bool accent, bool slide,
               uint8_t midiNote, uint8_t velocity);
    void noteOnLive(float frequency, bool accent, bool slide,
                    uint8_t midiNote, uint8_t velocity);
    void noteOff(uint8_t midiNote);
    void releaseAll();
    // Performance-mode panic used when a latched Drone/Lead voice must stop.
    // Unlike noteOff(), this also stops the intentionally note-off-agnostic
    // legacy MG/303 voice; ordinary MG/303 sequencing remains unchanged.
    void hardStop();
    void sustainLegacy();
    void releaseSequenced();
    void prepareStep(bool hasNotes, bool legato);
    float render();
    void setCutoff(float normalized);
    void setResonance(float normalized);
    void setVolume(float normalized);
    float cutoff() const;
    float resonance() const;
    float volume() const;
};

const char* synthEngineName(SynthEngine engine);
const char* synthFilterModeName(SynthFilterMode mode);
const char* synthLfoDestinationName(SynthLfoDestination destination);
