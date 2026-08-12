// ============================================================
// CardputerGroovebox - drum_voice.h
// 808-style and 909-style synthesized drums + sample lanes.
// 8 lanes, each independently set to 808 / 909 / SAMPLE.
// 808 engine derived from qwertyuu/Cardputer-Adv-Tracker (MIT);
// 909 engine and lane/choke system are new.
// ============================================================
#pragma once
#include "config.h"
#include "sampler.h"
#include <Arduino.h>
#include <math.h>

// ---------- Synthesized drum voice (shared state, per-lane) ----------
struct DrumSynthVoice {
    float phase, phase2;      // second osc for 909 snare
    float freq;
    float pitchEnv;
    float ampEnv;
    float decayMul;
    float pitchDecMul;
    float noiseAmt;
    float toneMix;
    float hpState;            // simple one-pole HP for 909 hats/snare noise
    float hpCoef;
    float drive;              // waveshaper amount (909 kick)
    float clapTimer;          // 808 clap retrigger
    int   clapCount;
    bool  is909;
    uint8_t type;
    bool  active;
    uint32_t noiseSeed;

    void init() {
        phase = phase2 = 0; freq = 60; pitchEnv = 0; ampEnv = 0;
        decayMul = 0.9995f; pitchDecMul = 0.999f;
        noiseAmt = 0; toneMix = 1.0f; hpState = 0; hpCoef = 0.05f;
        drive = 0; clapTimer = 0; clapCount = 0;
        is909 = false; type = DT_KICK; active = false; noiseSeed = 12345;
    }

    inline float noise() {
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        return ((float)(noiseSeed >> 16) / 32768.0f) - 1.0f;
    }

    // decayScale: 0.5..2.0 user decay control, tune: -12..+12 semitones
    void trigger(bool eng909, uint8_t t, float decayScale, float tune) {
        is909 = eng909; type = t;
        phase = phase2 = 0;
        ampEnv = 1.0f;
        active = true;
        clapCount = 0; clapTimer = 0;
        noiseSeed = (uint32_t)(micros() * 7u + t * 13u);
        float tuneMul = powf(2.0f, tune / 12.0f);

        if (!eng909) {  // ----- 808 -----
            switch (t) {
                case DT_KICK:
                    freq = 50.0f * tuneMul; pitchEnv = 180.0f;
                    decayMul = 0.9996f; pitchDecMul = 0.998f;
                    noiseAmt = 0.05f; toneMix = 1.0f; drive = 0; hpCoef = 0.02f;
                    break;
                case DT_SNARE:
                    freq = 180.0f * tuneMul; pitchEnv = 80.0f;
                    decayMul = 0.9990f; pitchDecMul = 0.996f;
                    noiseAmt = 0.6f; toneMix = 0.5f; drive = 0; hpCoef = 0.15f;
                    break;
                case DT_HAT_C:
                    freq = 0; pitchEnv = 0;
                    decayMul = 0.9975f; pitchDecMul = 1.0f;
                    noiseAmt = 1.0f; toneMix = 0.0f; drive = 0; hpCoef = 0.5f;
                    break;
                default: // CLAP
                    freq = 0; pitchEnv = 0;
                    decayMul = 0.9985f; pitchDecMul = 1.0f;
                    noiseAmt = 1.0f; toneMix = 0.0f; drive = 0; hpCoef = 0.25f;
                    clapCount = 3; clapTimer = 0;
                    break;
            }
        } else {        // ----- 909 -----
            switch (t) {
                case DT_KICK:   // punchier, driven, faster/deeper sweep
                    freq = 55.0f * tuneMul; pitchEnv = 320.0f;
                    decayMul = 0.9994f; pitchDecMul = 0.9962f;
                    noiseAmt = 0.10f; toneMix = 1.0f; drive = 1.6f; hpCoef = 0.02f;
                    break;
                case DT_SNARE:  // two detuned tones + bright snappy noise
                    freq = 185.0f * tuneMul; pitchEnv = 60.0f;
                    decayMul = 0.9988f; pitchDecMul = 0.995f;
                    noiseAmt = 0.85f; toneMix = 0.45f; drive = 0.4f; hpCoef = 0.30f;
                    break;
                case DT_HAT_C:  // closed hat: bright, very short
                    freq = 0; pitchEnv = 0;
                    decayMul = 0.9965f; pitchDecMul = 1.0f;
                    noiseAmt = 1.0f; toneMix = 0.0f; drive = 0; hpCoef = 0.65f;
                    break;
                default:        // DT_HAT_O open hat: bright, long
                    freq = 0; pitchEnv = 0;
                    decayMul = 0.9993f; pitchDecMul = 1.0f;
                    noiseAmt = 1.0f; toneMix = 0.0f; drive = 0; hpCoef = 0.65f;
                    break;
            }
        }
        // user decay: pull decayMul toward 1.0 (longer) or away (shorter)
        decayMul = 1.0f - (1.0f - decayMul) / decayScale;
        if (decayMul >= 0.99999f) decayMul = 0.99999f;
    }

    void choke() { ampEnv *= 0.2f; decayMul = 0.994f; }   // fast fade, no click

    inline float render() {
        if (!active) return 0.0f;

        ampEnv *= decayMul;
        if (ampEnv < 0.002f) { ampEnv = 0; active = false; return 0.0f; }

        float out = 0.0f;

        // tonal part
        if (toneMix > 0.001f) {
            float f = freq + pitchEnv;
            pitchEnv *= pitchDecMul;
            phase += f * INV_SAMPLE_RATE;
            if (phase >= 1.0f) phase -= 1.0f;
            float tone = sinf(phase * TWO_PI_F);

            if (is909 && type == DT_SNARE) {          // second partial
                phase2 += (f * 1.78f) * INV_SAMPLE_RATE;
                if (phase2 >= 1.0f) phase2 -= 1.0f;
                tone = 0.7f * tone + 0.5f * sinf(phase2 * TWO_PI_F);
            }
            if (drive > 0.01f) {                       // soft clip
                float x = tone * (1.0f + drive);
                tone = x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
            }
            out += tone * toneMix;
        }

        // noise part (one-pole HP for brightness)
        if (noiseAmt > 0.001f) {
            float n = noise();
            hpState += hpCoef * (n - hpState);
            float hn = n - hpState;

            // 808 clap: 3 fast retriggered noise bursts
            if (!is909 && type == DT_HAT_O_OR_CLAP && clapCount > 0) {
                clapTimer += 1.0f;
                if (clapTimer > SAMPLE_RATE * 0.011f) {   // ~11 ms apart
                    clapTimer = 0; clapCount--;
                    ampEnv = 1.0f;
                }
            }
            out += hn * noiseAmt;
        }

        return out * ampEnv;
    }
};

// ---------- Lane: engine choice + params + both voices ----------
struct DrumLane {
    DrumEngine engine;
    uint8_t    type;         // DrumType (for synth engines)
    int8_t     sampleSlot;   // for ENG_SMPL, -1 = none
    float      volume;       // 0..1
    float      tune;         // semitones -12..+12 (synth) / pitch (sample)
    float      decay;        // 0.5..2.0 (synth engines only)
    uint8_t    chokeGroup;   // 0 = none; lanes sharing a group choke each other
    float      velocityGain; // transient per-hit gain, not a saved parameter

    DrumSynthVoice sv;
    SampleVoice    smp;

    void init(DrumEngine e, uint8_t t) {
        engine = e; type = t; sampleSlot = -1;
        volume = 0.85f; tune = 0.0f; decay = 1.0f; chokeGroup = 0;
        velocityGain = 1.0f;
        sv.init(); smp.init();
    }

    void trigger(uint8_t velocity = 127) {
        velocityGain = static_cast<float>(velocity) / 127.0f;
        if (engine == ENG_SMPL) {
            smp.trigger(sampleSlot, powf(2.0f, tune / 12.0f),
                        volume * velocityGain);
        } else {
            sv.trigger(engine == ENG_909, type, decay, tune);
        }
    }

    void choke() {
        if (engine == ENG_SMPL) smp.stop();
        else sv.choke();
    }

    inline float render() {
        if (engine == ENG_SMPL) return smp.render() * 1.0f;   // gain baked at trigger
        return sv.render() * volume * velocityGain;
    }
};
