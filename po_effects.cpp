#include "po_effects.h"

#include <string.h>

PoPatternEffects::PoPatternEffects() { clear(); }
void PoPatternEffects::clear() { memset(effects_, PO_FX_NONE, sizeof(effects_)); }
bool PoPatternEffects::set(uint8_t pattern, uint8_t step, PoEffect effect) {
    if (pattern >= 16 || step >= 16 || effect >= PO_FX_COUNT) return false;
    effects_[pattern][step] = effect; return true;
}
PoEffect PoPatternEffects::get(uint8_t pattern, uint8_t step) const {
    return pattern < 16 && step < 16 ? static_cast<PoEffect>(effects_[pattern][step]) : PO_FX_NONE;
}

PoEffectProcessor::PoEffectProcessor() { reset(); }
void PoEffectProcessor::reset() {
    memset(history_, 0, sizeof(history_)); write_ = 0; phase_ = 0;
    stepFrames_ = 2756; effect_ = PO_FX_NONE; requested_ = PO_FX_NONE;
}
void PoEffectProcessor::setStepFrames(uint16_t frames) {
    stepFrames_ = frames < 16 ? 16 : (frames > 1535 ? 1535 : frames);
}
void PoEffectProcessor::engage(PoEffect effect) {
    const uint8_t value = effect < PO_FX_COUNT ? effect : PO_FX_NONE;
    __atomic_store_n(&requested_, value, __ATOMIC_RELEASE);
}
PoEffect PoEffectProcessor::effect() const {
    return static_cast<PoEffect>(__atomic_load_n(&requested_, __ATOMIC_ACQUIRE));
}
int16_t PoEffectProcessor::delayed(uint16_t delay) const {
    return history_[(write_ - delay) & 2047u];
}
int16_t PoEffectProcessor::bounded(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

int16_t PoEffectProcessor::process(int16_t input) {
    const PoEffect requested = static_cast<PoEffect>(
        __atomic_load_n(&requested_, __ATOMIC_ACQUIRE));
    if (requested != effect_) { effect_ = requested; phase_ = 0; }
    history_[write_] = input;
    int32_t output = input;
    const uint16_t loop16 = stepFrames_;
    const uint16_t loop12 = static_cast<uint16_t>((stepFrames_ * 4u) / 3u);
    const uint16_t shortLoop = static_cast<uint16_t>(stepFrames_ / 2u);
    const uint16_t tinyLoop = static_cast<uint16_t>(stepFrames_ / 4u);
    switch (effect_) {
        case PO_FX_LOOP_16:       output = delayed(loop16); break;
        case PO_FX_LOOP_12:       output = delayed(loop12); break;
        case PO_FX_LOOP_SHORT:    output = delayed(shortLoop); break;
        case PO_FX_LOOP_SHORTEST: output = delayed(tinyLoop); break;
        case PO_FX_UNISON:        output = (input + delayed(110)) / 2; break;
        case PO_FX_UNISON_LOW:    output = (input + delayed(331)) / 2; break;
        case PO_FX_OCTAVE_UP:     output = delayed(static_cast<uint16_t>((phase_ * 2u) & 2047u)); break;
        case PO_FX_OCTAVE_DOWN:   output = delayed(static_cast<uint16_t>((phase_ / 2u) & 2047u)); break;
        case PO_FX_STUTTER_4:     output = delayed(static_cast<uint16_t>(phase_ % (stepFrames_ / 4u))); break;
        case PO_FX_STUTTER_3:     output = delayed(static_cast<uint16_t>(phase_ % (stepFrames_ / 3u))); break;
        case PO_FX_SCRATCH:       output = delayed(static_cast<uint16_t>((phase_ * 3u / 2u) & 2047u)); break;
        case PO_FX_SCRATCH_FAST:  output = delayed(static_cast<uint16_t>((phase_ * 3u) & 2047u)); break;
        case PO_FX_QUANTIZE_6_8:
            if ((phase_ % (stepFrames_ * 2u)) > (stepFrames_ * 3u / 2u)) output = 0;
            break;
        case PO_FX_RETRIGGER_PATTERN: output = delayed(static_cast<uint16_t>(phase_ % stepFrames_)); break;
        case PO_FX_REVERSE: output = delayed(static_cast<uint16_t>(stepFrames_ - 1u - (phase_ % stepFrames_))); break;
        case PO_FX_NONE:
        default: break;
    }
    write_ = static_cast<uint16_t>((write_ + 1u) & 2047u);
    ++phase_;
    return bounded(output);
}
