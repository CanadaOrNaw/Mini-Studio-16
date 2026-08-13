#include "../vocoder.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

namespace {

struct RenderResult { uint32_t hash; uint64_t energy; };

// The vocoder consumes settings at the audio-block boundary (syncSettings),
// never per sample, because applying them recomputes 9 transcendentals.
RenderResult render(const VocoderSettings &settings, bool modulator,
                    int frames = 22050) {
    Vocoder8Band v;
    assert(v.applySettings(settings));
    v.syncSettings();
    RenderResult result = {2166136261u, 0};
    for (int i = 0; i < frames; ++i) {
        const int16_t carrier =
            static_cast<int16_t>(sin(6.2831853 * 220.0 * i / 22050.0) * 25000);
        const int16_t mod = modulator
            ? static_cast<int16_t>(sin(6.2831853 * 110.0 * i / 22050.0) * 26000) : 0;
        const int16_t out = v.process(carrier, mod);
        result.hash = (result.hash ^ static_cast<uint16_t>(out)) * 16777619u;
        result.energy += static_cast<uint64_t>(
            static_cast<int32_t>(out) * static_cast<int32_t>(out));
    }
    return result;
}

VocoderSettings base() {
    Vocoder8Band v;
    VocoderSettings s = v.settings();
    s.enabled = 1; s.gate = 0;
    return s;
}

// Fraction of samples pinned at full scale, i.e. how hard the output clips.
double clippedFraction(const VocoderSettings &settings) {
    Vocoder8Band v;
    assert(v.applySettings(settings));
    v.syncSettings();
    int clipped = 0;
    const int frames = 8000;
    for (int i = 0; i < frames; ++i) {
        const int16_t carrier =
            static_cast<int16_t>(sin(6.2831853 * 220.0 * i / 22050.0) * 30000);
        const int16_t mod =
            static_cast<int16_t>(sin(6.2831853 * 110.0 * i / 22050.0) * 30000);
        const int16_t out = v.process(carrier, mod);
        if (out >= 32760 || out <= -32760) ++clipped;
    }
    return static_cast<double>(clipped) / frames;
}

}  // namespace

int main() {
    // A2-P1-4 regression: all EIGHT bands must be distinct and strictly
    // ordered at every formant setting. The shipped table ran to 5,600 Hz
    // and relied on a hard coefficient clamp, which pinned the 3,600 Hz and
    // 5,600 Hz bands to the same 3,474 Hz filter — an "eight-band" vocoder
    // that was really seven, with a formant control frozen over most of its
    // travel.
    for (int shift = -12; shift <= 12; ++shift) {
        Vocoder8Band v;
        VocoderSettings s = base();
        s.formantShift = static_cast<int8_t>(shift);
        assert(v.applySettings(s));
        v.syncSettings();
        float previous = 0.0f;
        for (uint8_t band = 0; band < 8; ++band) {
            const float hz = v.bandCenterHz(band);
            assert(hz > previous * 1.05f);      // strictly ordered, ~a semitone apart
            assert(hz > 20.0f && hz < 3500.0f); // inside the SVF's stable range
            previous = hz;
        }
    }
    {   // Distinctness is what the clamp used to destroy: prove the top two
        // bands never coincide, at the extremes of the formant range.
        Vocoder8Band v;
        VocoderSettings s = base();
        s.formantShift = 12;
        assert(v.applySettings(s)); v.syncSettings();
        assert(v.bandCenterHz(7) > v.bandCenterHz(6) * 1.1f);
        s.formantShift = -12;
        assert(v.applySettings(s)); v.syncSettings();
        assert(v.bandCenterHz(7) > v.bandCenterHz(6) * 1.1f);
    }

    // Deterministic, and formant shift audibly changes the render.
    const RenderResult a = render(base(), true);
    assert(a.hash == render(base(), true).hash);
    assert(a.energy > 1000000);
    VocoderSettings shifted = base(); shifted.formantShift = 12;
    assert(a.hash != render(shifted, true).hash);
    VocoderSettings shiftedDown = base(); shiftedDown.formantShift = -12;
    assert(a.hash != render(shiftedDown, true).hash);
    assert(render(shifted, true).hash != render(shiftedDown, true).hash);

    // A2-P2: every remaining control must actually change the output. The
    // previous test never varied resonance/attack/release/noise/gate, which
    // is how a gate knob that was inert across its whole range shipped.
    VocoderSettings tweak = base();
    tweak.resonance = 10;  const uint32_t lowRes = render(tweak, true).hash;
    tweak.resonance = 120; const uint32_t highRes = render(tweak, true).hash;
    assert(lowRes != highRes);
    tweak = base(); tweak.attack = 5;   const uint32_t fastAttack = render(tweak, true).hash;
    tweak.attack = 120;                 const uint32_t slowAttack = render(tweak, true).hash;
    assert(fastAttack != slowAttack);
    tweak = base(); tweak.release = 5;  const uint32_t fastRelease = render(tweak, true).hash;
    tweak.release = 120;                const uint32_t slowRelease = render(tweak, true).hash;
    assert(fastRelease != slowRelease);
    tweak = base(); tweak.noise = 0;    const uint32_t noNoise = render(tweak, true).hash;
    tweak.noise = 120;                  const uint32_t lotsOfNoise = render(tweak, true).hash;
    assert(noNoise != lotsOfNoise);

    // A2-P2: the gate is usable across its travel. It used to span 0..0.1
    // while the envelopes it gates sit at 0.18..0.45, so it did nothing at
    // any setting. A high gate must measurably reduce output energy.
    VocoderSettings openGate = base(); openGate.gate = 0;
    VocoderSettings shutGate = base(); shutGate.gate = 127;
    const RenderResult open = render(openGate, true);
    const RenderResult shut = render(shutGate, true);
    assert(open.hash != shut.hash);
    assert(shut.energy < open.energy);
    printf("vocoder gate energy: open=%llu shut=%llu\n",
           static_cast<unsigned long long>(open.energy),
           static_cast<unsigned long long>(shut.energy));

    // A2-P3: the output no longer hard-clips a quarter of all samples at the
    // shipped default resonance (a fixed 3x gain on a sum of eight band
    // products used to guarantee it).
    VocoderSettings loud = base(); loud.resonance = 70;
    const double clipped = clippedFraction(loud);
    printf("vocoder clipped fraction at default resonance: %.3f\n", clipped);
    assert(clipped < 0.05);

    // Range validation is available without constructing an instance
    // (A2-P1-1: the project validator must not put one on the loop stack).
    VocoderSettings bad = base(); bad.formantShift = 13;
    assert(!Vocoder8Band::validate(bad));
    assert(Vocoder8Band::validate(base()));
    Vocoder8Band v;
    assert(!v.applySettings(bad));

    // A modulator-free render is silent but must never be asserted as the
    // desired behaviour of a *source* selection — audio_engine.cpp keeps the
    // dry bus when a source cannot supply a modulator (A2-P1-3).
    assert(render(base(), false).energy == 0);

    printf("vocoder: 8 distinct bands, every control audible, no clipping\n");
    return 0;
}
