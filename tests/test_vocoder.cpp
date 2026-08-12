#include "../vocoder.h"
#include <assert.h>
#include <math.h>

static uint32_t render(int shift, bool modulator) {
    Vocoder8Band v; VocoderSettings s = v.settings();
    s.enabled = 1; s.formantShift = shift; s.gate = 0; assert(v.applySettings(s));
    uint32_t hash = 2166136261u; uint64_t energy = 0;
    for (int i = 0; i < 22050; ++i) {
        const int16_t carrier = static_cast<int16_t>(sin(6.2831853 * 220.0 * i / 22050.0) * 25000);
        const int16_t mod = modulator ? static_cast<int16_t>(sin(6.2831853 * 110.0 * i / 22050.0) * 26000) : 0;
        const int16_t out = v.process(carrier, mod);
        hash = (hash ^ static_cast<uint16_t>(out)) * 16777619u; energy += static_cast<int32_t>(out) * out;
    }
    if (modulator) assert(energy > 1000000); else assert(energy == 0);
    return hash;
}
int main() {
    const uint32_t a = render(0, true); assert(a == render(0, true));
    assert(a != render(12, true)); render(0, false);
    Vocoder8Band v; VocoderSettings s = v.settings(); s.formantShift = 13;
    assert(!v.applySettings(s));
    return 0;
}

