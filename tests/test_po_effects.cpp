#include "../po_effects.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

namespace {

PoEffectProcessor *makeProcessor(uint16_t stepFrames) {
    PoEffectProcessor *p = new PoEffectProcessor();
    assert(p->begin());
    p->setStepFrames(stepFrames);
    return p;
}

uint32_t hashEffect(PoEffect fx) {
    PoEffectProcessor *p = makeProcessor(64);
    p->engage(fx);
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 5000; ++i) {
        const int16_t in = static_cast<int16_t>(((i * 997) & 65535) - 32768);
        const int16_t out = p->process(in);
        hash ^= static_cast<uint16_t>(out); hash *= 16777619u;
    }
    delete p;
    return hash;
}

// A2-P2 regression support: feed a signal whose VALUE encodes its own index,
// so each output reveals which input sample the effect read. The average
// step of that index across a window is the playback rate: +1 is normal
// forward, +2 is an octave up, -1 is reverse at unity, 0 is a held sample
// (the DC bug the stutters used to produce).
double averageReadStep(PoEffect fx, uint16_t stepFrames, int settle, int window) {
    PoEffectProcessor *p = makeProcessor(stepFrames);
    p->engage(fx);
    for (int i = 0; i < settle; ++i)
        p->process(static_cast<int16_t>(i & 0x3FFF));
    int previous = -1;
    double total = 0.0;
    int counted = 0;
    for (int i = settle; i < settle + window; ++i) {
        const int value = p->process(static_cast<int16_t>(i & 0x3FFF));
        if (previous >= 0) {
            const int delta = value - previous;
            // Only sample the rate WITHIN a slice. Every slice-bounded
            // effect restarts its read position at the slice boundary, and
            // that one-per-slice discontinuity would otherwise dominate the
            // mean and hide the actual playback rate.
            if (delta > -5 && delta < 5) { total += delta; ++counted; }
        }
        previous = value;
    }
    delete p;
    return counted ? total / counted : 0.0;
}

// Count distinct output values in a window. A slice that is genuinely
// repeating sweeps through many values; the old held-DC behaviour produced
// one value per slice.
int distinctValues(PoEffect fx, uint16_t stepFrames, int settle, int window) {
    PoEffectProcessor *p = makeProcessor(stepFrames);
    p->engage(fx);
    for (int i = 0; i < settle; ++i)
        p->process(static_cast<int16_t>(i & 0x3FFF));
    int seen = 0, previous = -1;
    for (int i = settle; i < settle + window; ++i) {
        const int value = p->process(static_cast<int16_t>(i & 0x3FFF));
        if (value != previous) ++seen;
        previous = value;
    }
    delete p;
    return seen;
}

}  // namespace

int main() {
    PoPatternEffects pattern;
    assert(pattern.get(0, 0) == PO_FX_NONE);
    assert(pattern.set(15, 15, PO_FX_REVERSE));
    assert(pattern.get(15, 15) == PO_FX_REVERSE);
    assert(!pattern.set(16, 0, PO_FX_NONE));

    // Every effect is deterministic and audibly different from dry.
    const uint32_t dry = hashEffect(PO_FX_NONE);
    for (uint8_t effect = 0; effect < PO_FX_NONE; ++effect) {
        const uint32_t first = hashEffect(static_cast<PoEffect>(effect));
        const uint32_t second = hashEffect(static_cast<PoEffect>(effect));
        assert(first == second);
        assert(first != dry);
    }

    PoEffectProcessor guard;
    assert(guard.begin());
    guard.engage(PO_FX_REVERSE);
    assert(guard.effect() == PO_FX_REVERSE);
    guard.engage(static_cast<PoEffect>(255));
    assert(guard.effect() == PO_FX_NONE);

    // A2-P2: an un-begun processor is a clean pass-through, never a crash.
    {
        PoEffectProcessor unallocated;
        unallocated.engage(PO_FX_LOOP_16);
        assert(unallocated.process(1234) == 1234);
    }

    // A2-P2: the step length tracks tempo instead of being pinned to a
    // hardcoded 120 BPM value, and stays inside what the history can hold.
    assert(poEffectStepFramesForBpm(120, 22050) == 2756);
    assert(poEffectStepFramesForBpm(128, 22050) == 2583);
    assert(poEffectStepFramesForBpm(0, 22050) == 2756);          // guards /0
    assert(poEffectStepFramesForBpm(40, 22050) <= PoEffectProcessor::kMaxStepFrames);
    assert(poEffectStepFramesForBpm(3000, 22050) >= 16);
    {
        PoEffectProcessor p;
        assert(p.begin());
        p.setStepFrames(60000);
        assert(p.stepFrames() == PoEffectProcessor::kMaxStepFrames);
        p.setStepFrames(1);
        assert(p.stepFrames() == 16);
    }

    // A2-P2: a real 16th note at the default tempo must fit the history
    // buffer without aliasing — the old 2,048-frame buffer could not hold
    // one, so every step-locked effect silently wrapped.
    assert(poEffectStepFramesForBpm(128, 22050) < PoEffectProcessor::kHistoryFrames);

    // A2-P2: OCTAVE_UP plays forward at 2x and REVERSE plays backwards at
    // 1x. These two used to be swapped: OCTAVE_UP ran backwards at unity
    // (no pitch shift at all) and REVERSE ran forwards at double speed.
    const double octaveUp = averageReadStep(PO_FX_OCTAVE_UP, 512, 1200, 400);
    const double reverse = averageReadStep(PO_FX_REVERSE, 512, 1200, 400);
    const double octaveDown = averageReadStep(PO_FX_OCTAVE_DOWN, 512, 1200, 400);
    printf("po read-steps: octaveUp=%.2f reverse=%.2f octaveDown=%.2f\n",
           octaveUp, reverse, octaveDown);
    assert(octaveUp > 1.5);      // forward, faster than realtime
    assert(reverse < -0.5);      // backwards
    assert(octaveDown > 0.2 && octaveDown < 0.8);   // forward, slower

    // A2-P2: the slice loopers repeat a captured window instead of holding a
    // single sample. The old `phase_ % step` delay produced a constant read
    // index for a whole slice — measured as one distinct value per slice.
    const int retrigger = distinctValues(PO_FX_RETRIGGER_PATTERN, 256, 600, 600);
    const int stutter4 = distinctValues(PO_FX_STUTTER_4, 256, 600, 600);
    const int stutter3 = distinctValues(PO_FX_STUTTER_3, 256, 600, 600);
    printf("po slice loops: retrigger=%d stutter4=%d stutter3=%d distinct/600\n",
           retrigger, stutter4, stutter3);
    assert(retrigger > 500);
    assert(stutter4 > 500);
    assert(stutter3 > 500);

    printf("po_effects: effects, step tempo mapping and playback rates passed\n");
    return 0;
}
