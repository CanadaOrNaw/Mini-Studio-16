#include "master_effects.h"
#include <string.h>

MasterEffects::MasterEffects() { reset(); }
void MasterEffects::reset() {
    memset(delay_, 0, sizeof(delay_)); write_ = 0; phase_ = 0; lowpass_ = 0;
    active_ = {}; active_.feedback = 55; active_.rate = 32; active_.filter = 100;
    for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i) active_.mix[i] = 48;
    pending_.enabledMask = active_.enabledMask; pending_.feedback = active_.feedback;
    pending_.rate = active_.rate; pending_.filter = active_.filter;
    for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i) pending_.mix[i] = active_.mix[i];
    sequence_ = 0; appliedSequence_ = 0;
}
bool MasterEffects::setEnabled(MasterEffectType e, bool on) {
    if (e >= MASTER_EFFECT_COUNT) return false;
    MasterEffectsSettings s = settings();
    if (on) s.enabledMask |= static_cast<uint8_t>(1u << e);
    else s.enabledMask &= static_cast<uint8_t>(~(1u << e));
    return applySettings(s);
}
bool MasterEffects::enabled(MasterEffectType e) const {
    return e < MASTER_EFFECT_COUNT && (settings().enabledMask & (1u << e));
}
bool MasterEffects::setMix(MasterEffectType e, uint8_t value) {
    if (e >= MASTER_EFFECT_COUNT || value > 127) return false;
    MasterEffectsSettings s = settings(); s.mix[e] = value; return applySettings(s);
}
bool MasterEffects::setFeedback(uint8_t value) {
    if (value > 120) return false;
    MasterEffectsSettings s = settings(); s.feedback = value; return applySettings(s);
}
bool MasterEffects::setRate(uint8_t value) {
    if (value == 0 || value > 127) return false;
    MasterEffectsSettings s = settings(); s.rate = value; return applySettings(s);
}
bool MasterEffects::setFilter(uint8_t value) {
    if (value == 0 || value > 127) return false;
    MasterEffectsSettings s = settings(); s.filter = value; return applySettings(s);
}
bool MasterEffects::applySettings(const MasterEffectsSettings &s) {
    if ((s.enabledMask & ~((1u << MASTER_EFFECT_COUNT) - 1u)) || s.feedback > 120 ||
        s.rate == 0 || s.rate > 127 || s.filter == 0 || s.filter > 127) return false;
    for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i) if (s.mix[i] > 127) return false;
    __atomic_add_fetch(&sequence_, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&pending_.enabledMask, s.enabledMask, __ATOMIC_RELAXED);
    for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i)
        __atomic_store_n(&pending_.mix[i], s.mix[i], __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.feedback, s.feedback, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.rate, s.rate, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.filter, s.filter, __ATOMIC_RELAXED);
    __atomic_add_fetch(&sequence_, 1u, __ATOMIC_RELEASE);
    return true;
}
MasterEffectsSettings MasterEffects::settings() const {
    MasterEffectsSettings out;
    for (;;) {
        const uint32_t before = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        out.enabledMask = __atomic_load_n(&pending_.enabledMask, __ATOMIC_RELAXED);
        for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i)
            out.mix[i] = __atomic_load_n(&pending_.mix[i], __ATOMIC_RELAXED);
        out.feedback = __atomic_load_n(&pending_.feedback, __ATOMIC_RELAXED);
        out.rate = __atomic_load_n(&pending_.rate, __ATOMIC_RELAXED);
        out.filter = __atomic_load_n(&pending_.filter, __ATOMIC_RELAXED);
        if (before == __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE)) return out;
    }
}
void MasterEffects::syncPending() {
    const uint32_t sequence = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
    if (sequence == appliedSequence_ || (sequence & 1u)) return;
    active_ = settings(); appliedSequence_ = sequence;
}
int16_t MasterEffects::tap(uint16_t delay) const { return delay_[(write_ - delay) & 4095u]; }
int16_t MasterEffects::clamp(int32_t v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : v; }
int16_t MasterEffects::blend(int16_t dry, int16_t wet, uint8_t mix) const {
    return clamp((static_cast<int32_t>(dry) * (127 - mix) + static_cast<int32_t>(wet) * mix) / 127);
}
int16_t MasterEffects::process(int16_t input) {
    syncPending();
    int16_t value = input;
    const uint16_t triangle = static_cast<uint16_t>((phase_ >> 8) & 511u);
    const uint16_t lfo = triangle < 256 ? triangle : 511 - triangle;
    if (active_.enabledMask & (1u << MASTER_REVERB)) {
        const int32_t wet = (tap(743) + tap(1499) + tap(2111) + tap(3371)) / 4;
        value = blend(value, clamp(wet), active_.mix[MASTER_REVERB]);
    }
    if (active_.enabledMask & (1u << MASTER_DELAY)) value = blend(value, tap(3307), active_.mix[MASTER_DELAY]);
    if (active_.enabledMask & (1u << MASTER_CHORUS)) value = blend(value, tap(static_cast<uint16_t>(330 + lfo)), active_.mix[MASTER_CHORUS]);
    if (active_.enabledMask & (1u << MASTER_FLANGER)) value = blend(value, tap(static_cast<uint16_t>(20 + lfo / 2)), active_.mix[MASTER_FLANGER]);
    if (active_.enabledMask & (1u << MASTER_TREMOLO)) {
        const int32_t gain = 64 + lfo / 4;
        value = blend(value, clamp(static_cast<int32_t>(value) * gain / 127), active_.mix[MASTER_TREMOLO]);
    }
    if (active_.enabledMask & (1u << MASTER_VIBRATO)) value = blend(value, tap(static_cast<uint16_t>(80 + lfo)), active_.mix[MASTER_VIBRATO]);
    if (active_.enabledMask & (1u << MASTER_FILTER)) {
        lowpass_ += (static_cast<int32_t>(value) - lowpass_) * active_.filter / 128;
        value = blend(value, clamp(lowpass_), active_.mix[MASTER_FILTER]);
    }
    const int32_t feedback = static_cast<int32_t>(tap(3307)) * active_.feedback / 127;
    delay_[write_] = clamp(static_cast<int32_t>(input) + feedback);
    write_ = static_cast<uint16_t>((write_ + 1u) & 4095u);
    phase_ += active_.rate;
    return value;
}
