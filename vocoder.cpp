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
bool Vocoder8Band::applySettings(const VocoderSettings &s) {
    if (s.enabled > 1 || s.source > VOCODER_LINE || s.formantShift < -12 ||
        s.formantShift > 12 || s.resonance > 127 || s.attack > 127 ||
        s.release > 127 || s.noise > 127 || s.gate > 127) return false;
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
    VocoderSettings out;
    for (;;) {
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
}
void Vocoder8Band::syncPending() {
    const uint32_t sequence = __atomic_load_n(&sequence_, __ATOMIC_ACQUIRE);
    if (sequence == appliedSequence_ || (sequence & 1u)) return;
    active_ = settings(); appliedSequence_ = sequence; updateCoefficients();
}
void Vocoder8Band::updateCoefficients() {
    static const float centers[8] = {120,240,480,850,1400,2300,3600,5600};
    const float ratio = powf(2.0f, active_.formantShift / 12.0f);
    for (uint8_t i = 0; i < 8; ++i) {
        const float hz = centers[i] * ratio;
        float coefficient = 2.0f * sinf(3.14159265359f * hz / SAMPLE_RATE);
        if (coefficient > 0.95f) coefficient = 0.95f;
        bands_[i].coefficient = coefficient;
    }
}
int16_t Vocoder8Band::process(int16_t carrierPcm, int16_t modulatorPcm) {
    syncPending();
    if (!active_.enabled) return carrierPcm;
    float carrier = carrierPcm / 32768.0f, modulator = modulatorPcm / 32768.0f;
    noiseState_ = noiseState_ * 1664525u + 1013904223u;
    const float noise = (static_cast<int32_t>(noiseState_ >> 16) - 32768) / 32768.0f;
    carrier += noise * active_.noise / 1270.0f;
    const float damping = 0.22f + (127 - active_.resonance) / 180.0f;
    const float attack = 0.002f + active_.attack / 2500.0f;
    const float release = 0.0002f + active_.release / 12000.0f;
    const float gate = active_.gate / 1270.0f;
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
    output *= 3.0f;
    if (output > 1.0f) output = 1.0f; else if (output < -1.0f) output = -1.0f;
    return static_cast<int16_t>(output * 32767.0f);
}
