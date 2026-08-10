#pragma once

#include "config.h"

#include <stdint.h>

extern const int16_t g_synthSineTable[256];

// A 32-bit wrapping phase accumulator with an interpolated 256-point table.
// Modulation is supplied in cycles, so 1.0 is a full turn.  No transcendental
// function is used in the per-sample path.
static inline float synthSine(uint32_t phase, float modulationCycles = 0.0f) {
    const float base = static_cast<float>(phase >> 8) * (1.0f / 65536.0f);
    const float position = base + modulationCycles * 256.0f;
    int32_t whole = static_cast<int32_t>(position);
    if (position < static_cast<float>(whole)) --whole;
    const float fraction = position - static_cast<float>(whole);
    const uint8_t first = static_cast<uint8_t>(whole);
    const uint8_t second = static_cast<uint8_t>(first + 1u);
    const float a = static_cast<float>(g_synthSineTable[first]);
    const float b = static_cast<float>(g_synthSineTable[second]);
    return (a + (b - a) * fraction) * (1.0f / 32768.0f);
}

static inline uint32_t synthPhaseIncrement(float frequency) {
    if (!(frequency > 0.0f)) return 0;
    const float maximum = static_cast<float>(SAMPLE_RATE) * 0.45f;
    if (frequency > maximum) frequency = maximum;
    return static_cast<uint32_t>(frequency *
        (4294967296.0f / static_cast<float>(SAMPLE_RATE)));
}

enum SynthEnvelopeStage : uint8_t {
    SYNTH_ENV_IDLE = 0,
    SYNTH_ENV_ATTACK,
    SYNTH_ENV_DECAY,
    SYNTH_ENV_SUSTAIN,
    SYNTH_ENV_RELEASE,
};
struct SynthAdsrParams {
    uint16_t attackMs;
    uint16_t decayMs;
    uint16_t releaseMs;
    float sustain;

    void set(uint16_t attack, uint16_t decay, float sustainLevel,
             uint16_t release) {
        attackMs = attack;
        decayMs = decay;
        sustain = sustainLevel;
        releaseMs = release;
    }
};

struct SynthAdsr {
    float value;
    float attackStep;
    float decayStep;
    float releaseStep;
    SynthEnvelopeStage stage;

    void init() {
        value = 0.0f;
        attackStep = decayStep = releaseStep = 0.0f;
        stage = SYNTH_ENV_IDLE;
    }

    void noteOn(const SynthAdsrParams& params, bool retrigger = true) {
        if (retrigger) value = 0.0f;
        const uint32_t samples = static_cast<uint32_t>(params.attackMs) *
                                 static_cast<uint32_t>(SAMPLE_RATE) / 1000u;
        if (samples == 0) {
            value = 1.0f;
            beginDecay(params);
        } else {
            attackStep = (1.0f - value) / static_cast<float>(samples);
            stage = SYNTH_ENV_ATTACK;
        }
    }

    void noteOff(const SynthAdsrParams& params) {
        if (stage == SYNTH_ENV_IDLE || stage == SYNTH_ENV_RELEASE) return;
        const uint32_t samples = static_cast<uint32_t>(params.releaseMs) *
                                 static_cast<uint32_t>(SAMPLE_RATE) / 1000u;
        if (samples == 0 || value <= 0.000001f) {
            value = 0.0f;
            stage = SYNTH_ENV_IDLE;
        } else {
            releaseStep = value / static_cast<float>(samples);
            stage = SYNTH_ENV_RELEASE;
        }
    }

    float render(const SynthAdsrParams& params) {
        switch (stage) {
            case SYNTH_ENV_ATTACK:
                value += attackStep;
                if (value >= 1.0f) {
                    value = 1.0f;
                    beginDecay(params);
                }
                break;
            case SYNTH_ENV_DECAY:
                value -= decayStep;
                if (value <= params.sustain) {
                    value = params.sustain;
                    stage = SYNTH_ENV_SUSTAIN;
                }
                break;
            case SYNTH_ENV_RELEASE:
                value -= releaseStep;
                if (value <= 0.000001f) {
                    value = 0.0f;
                    stage = SYNTH_ENV_IDLE;
                }
                break;
            default:
                break;
        }
        return value;
    }

    bool active() const { return stage != SYNTH_ENV_IDLE; }

private:
    void beginDecay(const SynthAdsrParams& params) {
        const uint32_t samples = static_cast<uint32_t>(params.decayMs) *
                                 static_cast<uint32_t>(SAMPLE_RATE) / 1000u;
        if (samples == 0 || params.sustain >= 1.0f) {
            value = params.sustain;
            stage = SYNTH_ENV_SUSTAIN;
        } else {
            decayStep = (1.0f - params.sustain) / static_cast<float>(samples);
            stage = SYNTH_ENV_DECAY;
        }
    }
};
