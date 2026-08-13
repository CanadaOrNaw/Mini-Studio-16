#pragma once

#include <stdint.h>

enum PoEffect : uint8_t {
    PO_FX_LOOP_16 = 0, PO_FX_LOOP_12, PO_FX_LOOP_SHORT, PO_FX_LOOP_SHORTEST,
    PO_FX_UNISON, PO_FX_UNISON_LOW, PO_FX_OCTAVE_UP, PO_FX_OCTAVE_DOWN,
    PO_FX_STUTTER_4, PO_FX_STUTTER_3, PO_FX_SCRATCH, PO_FX_SCRATCH_FAST,
    PO_FX_QUANTIZE_6_8, PO_FX_RETRIGGER_PATTERN, PO_FX_REVERSE, PO_FX_NONE,
    PO_FX_COUNT
};

class PoPatternEffects {
public:
    PoPatternEffects();
    void clear();
    bool set(uint8_t pattern, uint8_t step, PoEffect effect);
    PoEffect get(uint8_t pattern, uint8_t step) const;
private:
    uint8_t effects_[16][16];
};

// Bounded punch-in processor. It never allocates or calls storage from
// process(). Effects are performance-oriented transformations, not
// oscillator detune aliases.
//
// A2-P2 (alpha.2 reconciliation): the history buffer is 8,192 frames and
// heap-allocated by begin(). It used to be a 2,048-frame static array,
// which (a) charged 4 KiB against a static-DRAM budget that had only 72
// bytes of headroom, and (b) was too short to hold even a single 16th note
// at the default 128 BPM (2,584 frames) — so every step-locked effect
// silently aliased through the `& 2047` mask. Steps are clamped to half the
// buffer so REVERSE, which reads two step-lengths back, always fits.
class PoEffectProcessor {
public:
    static const uint16_t kHistoryFrames = 8192;
    static const uint16_t kMaxStepFrames = kHistoryFrames / 2;

    PoEffectProcessor();
    ~PoEffectProcessor();
    // Allocates the history buffer. Call once from setup(); process() is a
    // clean pass-through until it succeeds, so a failed allocation degrades
    // to "no punch effects" rather than crashing.
    bool begin();
    bool ready() const { return history_ != nullptr; }
    void reset();
    // Step length in frames, normally one 16th note. Recomputed by the
    // sequencer whenever the tempo changes (see poEffectsUpdateStepFrames).
    void setStepFrames(uint16_t frames);
    uint16_t stepFrames() const { return stepFrames_; }
    void engage(PoEffect effect);
    PoEffect effect() const;
    int16_t process(int16_t input);
private:
    int16_t *history_;
    uint16_t write_;
    uint32_t phase_;
    uint16_t stepFrames_;
    PoEffect effect_;
    volatile uint8_t requested_;
    int16_t delayed(uint32_t delay) const;
    static int16_t bounded(int32_t value);
};

// Frames per 16th note at the given tempo, clamped to what the processor can
// represent. Shared with the host tests.
uint16_t poEffectStepFramesForBpm(uint16_t bpm, uint32_t sampleRate);
