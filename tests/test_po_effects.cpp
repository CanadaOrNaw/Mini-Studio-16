#include "../po_effects.h"
#include <assert.h>
#include <stdint.h>

static uint32_t hashEffect(PoEffect fx) {
    PoEffectProcessor p; p.setStepFrames(64); p.engage(fx);
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 5000; ++i) {
        const int16_t in = static_cast<int16_t>(((i * 997) & 65535) - 32768);
        const int16_t out = p.process(in);
        hash ^= static_cast<uint16_t>(out); hash *= 16777619u;
    }
    return hash;
}

int main() {
    PoPatternEffects pattern;
    assert(pattern.get(0, 0) == PO_FX_NONE);
    assert(pattern.set(15, 15, PO_FX_REVERSE));
    assert(pattern.get(15, 15) == PO_FX_REVERSE);
    assert(!pattern.set(16, 0, PO_FX_NONE));
    const uint32_t dry = hashEffect(PO_FX_NONE);
    for (uint8_t effect = 0; effect < PO_FX_NONE; ++effect) {
        const uint32_t first = hashEffect(static_cast<PoEffect>(effect));
        const uint32_t second = hashEffect(static_cast<PoEffect>(effect));
        assert(first == second);
        assert(first != dry);
    }
    PoEffectProcessor p; p.engage(PO_FX_REVERSE);
    assert(p.effect() == PO_FX_REVERSE);
    p.engage(static_cast<PoEffect>(255));
    assert(p.effect() == PO_FX_NONE);
    return 0;
}
