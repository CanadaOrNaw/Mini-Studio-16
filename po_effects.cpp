#include "po_effects.h"

#include <stdlib.h>
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

uint16_t poEffectStepFramesForBpm(uint16_t bpm, uint32_t sampleRate) {
    if (bpm == 0) bpm = 120;
    // One 16th note: sampleRate * 60 / (bpm * 4).
    uint32_t frames = (sampleRate * 60u) / (static_cast<uint32_t>(bpm) * 4u);
    if (frames < 16u) frames = 16u;
    if (frames > PoEffectProcessor::kMaxStepFrames)
        frames = PoEffectProcessor::kMaxStepFrames;
    return static_cast<uint16_t>(frames);
}

PoEffectProcessor::PoEffectProcessor() : history_(nullptr) { reset(); }
PoEffectProcessor::~PoEffectProcessor() { free(history_); history_ = nullptr; }

bool PoEffectProcessor::begin() {
    if (history_) return true;
    history_ = static_cast<int16_t *>(calloc(kHistoryFrames, sizeof(int16_t)));
    return history_ != nullptr;
}

void PoEffectProcessor::reset() {
    if (history_) memset(history_, 0, kHistoryFrames * sizeof(int16_t));
    write_ = 0; phase_ = 0;
    // Default to one 16th at 120 BPM; the sequencer overwrites this from the
    // live tempo on the first tick (A2-P2: setStepFrames previously had no
    // caller at all outside the tests, so the value never tracked BPM).
    stepFrames_ = 2756;
    if (stepFrames_ > kMaxStepFrames) stepFrames_ = kMaxStepFrames;
    effect_ = PO_FX_NONE; requested_ = PO_FX_NONE;
}
void PoEffectProcessor::setStepFrames(uint16_t frames) {
    stepFrames_ = frames < 16 ? 16 : (frames > kMaxStepFrames ? kMaxStepFrames : frames);
}
void PoEffectProcessor::engage(PoEffect effect) {
    const uint8_t value = effect < PO_FX_COUNT ? effect : PO_FX_NONE;
    __atomic_store_n(&requested_, value, __ATOMIC_RELEASE);
}
PoEffect PoEffectProcessor::effect() const {
    return static_cast<PoEffect>(__atomic_load_n(&requested_, __ATOMIC_ACQUIRE));
}
int16_t PoEffectProcessor::delayed(uint32_t delay) const {
    if (delay >= kHistoryFrames) delay = kHistoryFrames - 1u;
    return history_[(write_ - delay) & (kHistoryFrames - 1u)];
}
int16_t PoEffectProcessor::bounded(int32_t value) {
    if (value > 32767) return 32767;
    if (value < -32768) return -32768;
    return static_cast<int16_t>(value);
}

namespace {
// A2-P2: repeat a captured slice. `delayed()` is relative to the advancing
// write pointer, so a *constant* read position needs a delay that grows one
// step at a time — the old `phase_ % step` did the opposite and produced a
// single held sample (DC) for the whole slice. Delay is capped so the slice
// being replayed is still inside the history buffer.
uint32_t sliceLoopDelay(uint32_t phase, uint32_t step, uint32_t historyFrames) {
    if (step == 0) return 0;
    uint32_t repeats = phase / step;
    const uint32_t maximum = historyFrames / step;
    if (maximum > 1u && repeats > maximum - 1u) repeats = maximum - 1u;
    return repeats * step;
}
}  // namespace

int16_t PoEffectProcessor::process(int16_t input) {
    const PoEffect requested = static_cast<PoEffect>(
        __atomic_load_n(&requested_, __ATOMIC_ACQUIRE));
    if (requested != effect_) { effect_ = requested; phase_ = 0; }
    if (!history_) return input;          // begin() failed; clean pass-through
    history_[write_] = input;
    int32_t output = input;
    const uint32_t step = stepFrames_ ? stepFrames_ : 1u;
    if (effect_ != PO_FX_NONE) {
        switch (effect_) {
            case PO_FX_LOOP_16:       output = delayed(step); break;
            case PO_FX_LOOP_12:       output = delayed((step * 4u) / 3u); break;
            case PO_FX_LOOP_SHORT:    output = delayed(step / 2u); break;
            case PO_FX_LOOP_SHORTEST: output = delayed(step / 4u); break;
            case PO_FX_UNISON:        output = (input + delayed(110)) / 2; break;
            case PO_FX_UNISON_LOW:    output = (input + delayed(331)) / 2; break;
            // A2-P2: OCTAVE_UP and REVERSE used to be swapped. The read
            // pointer moves at (1 - d'/dt) samples per output sample, so a
            // delay that DECREASES by one per sample plays forward at 2x
            // (an octave up), and a delay that INCREASES by two per sample
            // plays backwards at 1x. Measured read-pointer traces confirmed
            // the old OCTAVE_UP ran backwards at 1x and the old REVERSE ran
            // forwards at 2x.
            case PO_FX_OCTAVE_UP:
                output = delayed(step - 1u - (phase_ % step)); break;
            // A2-P2: the free-running effects are slice-bounded so their
            // delay can never walk past the history buffer (where it used to
            // clamp into a fixed long delay after a fraction of a second).
            case PO_FX_OCTAVE_DOWN:
                output = delayed((phase_ % (step * 2u)) / 2u); break;
            case PO_FX_REVERSE:
                output = delayed(2u * (phase_ % step)); break;
            case PO_FX_STUTTER_4:
                output = delayed(sliceLoopDelay(phase_, step / 4u, kHistoryFrames)); break;
            case PO_FX_STUTTER_3:
                output = delayed(sliceLoopDelay(phase_, step / 3u, kHistoryFrames)); break;
            case PO_FX_RETRIGGER_PATTERN:
                output = delayed(sliceLoopDelay(phase_, step, kHistoryFrames)); break;
            case PO_FX_SCRATCH:
                output = delayed(((phase_ % step) * 3u) / 2u); break;
            case PO_FX_SCRATCH_FAST:
                output = delayed((phase_ % (step / 2u ? step / 2u : 1u)) * 3u); break;
            case PO_FX_QUANTIZE_6_8:
                if ((phase_ % (step * 2u)) > (step * 3u / 2u)) output = 0;
                break;
            default: break;
        }
    }
    write_ = static_cast<uint16_t>((write_ + 1u) & (kHistoryFrames - 1u));
    ++phase_;
    return bounded(output);
}
