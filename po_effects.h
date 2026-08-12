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

// Bounded punch-in processor. It owns 4096 mono history frames and never
// allocates or calls storage from process(). Effects are intentionally
// performance-oriented transformations, not oscillator detune aliases.
class PoEffectProcessor {
public:
    PoEffectProcessor();
    void reset();
    void setStepFrames(uint16_t frames);
    void engage(PoEffect effect);
    PoEffect effect() const;
    int16_t process(int16_t input);
private:
    int16_t history_[2048];
    uint16_t write_;
    uint32_t phase_;
    uint16_t stepFrames_;
    PoEffect effect_;
    volatile uint8_t requested_;
    int16_t delayed(uint16_t delay) const;
    static int16_t bounded(int32_t value);
};
