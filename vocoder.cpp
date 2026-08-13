#include "vocoder.h"
#include "config.h"
#include <math.h>
#include <string.h>

Vocoder8Band::Vocoder8Band() { reset(); }
void Vocoder8Band::reset() {
    memset(bands_, 0, sizeof(bands_));
    active_ = {0, VOCODER_LOOP1, 0, 70, 50, 35, 10, 4};
    pending_.enabled = active_.enabled; pending_.source = active_.source;
    pending_.formantShift = active_.formantShift; pending_.resonance = active_.resonance;
    pending_.attack = active_.attack; pending_.release = active_.release;
    pending_.noise = active_.noise; pending_.gate = active_.gate;
    sequence_ = 0; appliedSequence_ = 0;
    noiseState_ = 1; updateCoefficients();
}
bool Vocoder8Band::validate(const VocoderSettings &s) {
    return !(s.enabled > 1 || s.source > VOCODER_LINE || s.formantShift < -12 ||
             s.formantShift > 12 || s.resonance > 127 || s.attack > 127 ||
             s.release > 127 || s.noise > 127 || s.gate > 127);
}
bool Vocoder8Band::applySettings(const VocoderSettings &s) {
    if (!validate(s)) return false;
    __atomic_add_fetch(&sequence_, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&pending_.enabled, s.enabled, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.source, s.source, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.formantShift, s.formantShift, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.resonance, s.resonance, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.attack, s.attack, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.release, s.release, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.noise, s.noise, __ATOMIC_RELAXED);
    __atomic_store_n(&pending_.gate, s.gate, __ATOMIC_RELAXED);
    __atomic_add_fetch(&sequence_, 1u, __ATOMIC_RELEASE);
    return true;
}
VocoderSettings Vocoder8Band::settings() const {
    VocoderSettings out = {};
    // A2-P3: bounded, like MasterEffects::settings — never spin the audio
    // task while the UI task is mid-write.
    for (uint8_t attempt = 0; attempt < 4; ++attempt) {
        const uint32_t before = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
        if (before & 1u) continue;
        out.enabled = __atomic_load_n(&pending_.enabled, __ATOMIC_RELAXED);
        out.source = __atomic_load_n(&pending_.source, __ATOMIC_RELAXED);
        out.formantShift = __atomic_load_n(&pending_.formantShift, __ATOMIC_RELAXED);
        out.resonance = __atomic_load_n(&pending_.resonance, __ATOMIC_RELAXED);
        out.attack = __atomic_load_n(&pending_.attack, __ATOMIC_RELAXED);
        out.release = __atomic_load_n(&pending_.release, __ATOMIC_RELAXED);
        out.noise = __atomic_load_n(&pending_.noise, __ATOMIC_RELAXED);
        out.gate = __atomic_load_n(&pending_.gate, __ATOMIC_RELAXED);
        if (before == __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE)) return out;
    }
    return active_;  // writer is mid-update; keep the running settings
}
void Vocoder8Band::syncPending() {
    const uint32_t sequence = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
    if (sequence == appliedSequence_ || (sequence & 1u)) return;
    active_ = settings(); appliedSequence_ = sequence; updateCoefficients();
}
void Vocoder8Band::updateCoefficients() {
    // A2-P1-4 (alpha.2 reconciliation): a Chamberlin state-variable filter
    // is only stable while 2*sin(pi*fc/fs) + damping < 2, i.e. fc below
    // about fs/6 = 3675 Hz at 22.05 kHz. The previous centre table ran to
    // 5600 Hz and relied on a hard `coefficient > 0.95` clamp, which pinned
    // BOTH the 3600 Hz and 5600 Hz bands to the same 3474 Hz coefficient —
    // the "eight-band" vocoder was really seven, and formant shift was
    // frozen for 21 of its 25 settings. (Same missing-scale class of bug as
    // round one's MGX cutoff dead zone.) The centres below all fit under
    // kMaxBandHz, and the formant ratio is clamped so the top band stays
    // representable, which keeps all eight bands distinct and ordered at
    // every setting. Downward shifts are unrestricted.
    static const float centers[8] = {120, 210, 360, 600, 950, 1450, 2050, 2750};
    static const float kMaxBandHz = 3400.0f;   // ~fs/6.5, stability limit
    float ratio = powf(2.0f, active_.formantShift / 12.0f);
    const float maxRatio = kMaxBandHz / centers[7];
    if (ratio > maxRatio) ratio = maxRatio;
    for (uint8_t i = 0; i < 8; ++i) {
        const float hz = centers[i] * ratio;
        bands_[i].centerHz = hz;
        float coefficient = 2.0f * sinf(3.14159265359f * hz / SAMPLE_RATE);
        if (coefficient > 0.95f) coefficient = 0.95f;  // unreachable safety net
        bands_[i].coefficient = coefficient;
    }
}
float Vocoder8Band::bandCenterHz(uint8_t band) const {
    return band < 8 ? bands_[band].centerHz : 0.0f;
}
void Vocoder8Band::syncSettings() { syncPending(); }
int16_t Vocoder8Band::process(int16_t carrierPcm, int16_t modulatorPcm) {
    // A2-P3: settings (and the 9 transcendentals in updateCoefficients) are
    // consumed by syncSettings() at the block boundary, not per sample.
    if (!active_.enabled) return carrierPcm;
    float carrier = carrierPcm / 32768.0f, modulator = modulatorPcm / 32768.0f;
    noiseState_ = noiseState_ * 1664525u + 1013904223u;
    const float noise = (static_cast<int32_t>(noiseState_ >> 16) - 32768) / 32768.0f;
    carrier += noise * active_.noise / 1270.0f;
    const float damping = 0.22f + (127 - active_.resonance) / 180.0f;
    const float attack = 0.002f + active_.attack / 2500.0f;
    const float release = 0.0002f + active_.release / 12000.0f;
    // A2-P2: the gate threshold used to span 0..0.1 while the band
    // envelopes it is compared against sit at 0.18..0.45, so the knob was
    // inert across its entire travel. 0..0.5 makes it usable.
    const float gate = active_.gate / 254.0f;
    float output = 0.0f;
    for (uint8_t i = 0; i < 8; ++i) {
        Band &b = bands_[i]; const float f = b.coefficient;
        b.cLow += f * b.cBand;
        const float cHigh = carrier - b.cLow - damping * b.cBand;
        b.cBand += f * cHigh;
        b.mLow += f * b.mBand;
        const float mHigh = modulator - b.mLow - damping * b.mBand;
        b.mBand += f * mHigh;
        const float target = fabsf(b.mBand);
        b.envelope += (target - b.envelope) * (target > b.envelope ? attack : release);
        const float envelope = b.envelope > gate ? b.envelope : 0.0f;
        output += b.cBand * envelope;
    }
    // A2-P3: a fixed 3x gain on a sum of eight band products hard-clipped
    // roughly a quarter of all samples at the shipped default resonance,
    // which is also why the gate and noise controls were inaudible. Scale
    // by the band count instead. The inverted comparison also catches NaN
    // (a plain `>`/`<` pair does not), so a poisoned filter state can never
    // reach the int16 cast as undefined behaviour.
    // The sum is over eight band products, each of which can approach unity,
    // and raising resonance raises every band's Q (and therefore its
    // output). Normalise for the band count and compensate for resonance so
    // the level stays sane across the whole knob instead of clipping.
    output *= 0.34f - 0.16f * (static_cast<float>(active_.resonance) / 127.0f);
    if (!(output > -1.0f && output < 1.0f))
        output = output > 0.0f ? 1.0f : (output < 0.0f ? -1.0f : 0.0f);
    return static_cast<int16_t>(output * 32767.0f);
}
