#include "../master_effects.h"
#include <assert.h>

static uint32_t render(MasterEffectType effect) {
    MasterEffects fx; fx.setEnabled(effect, true); fx.setMix(effect, 100);
    uint32_t hash = 2166136261u;
    for (int i = 0; i < 20000; ++i) {
        const int16_t out = fx.process(i == 0 ? 24000 : static_cast<int16_t>((i * 37) % 4000 - 2000));
        hash = (hash ^ static_cast<uint16_t>(out)) * 16777619u;
    }
    return hash;
}
int main() {
    MasterEffects dry;
    uint32_t dryHash = 2166136261u;
    for (int i = 0; i < 20000; ++i)
        dryHash = (dryHash ^ static_cast<uint16_t>(dry.process(
            i == 0 ? 24000 : static_cast<int16_t>((i * 37) % 4000 - 2000)))) * 16777619u;
    for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i) {
        const uint32_t a = render(static_cast<MasterEffectType>(i));
        assert(a == render(static_cast<MasterEffectType>(i)) && a != dryHash);
    }
    MasterEffects fx; MasterEffectsSettings settings = fx.settings();
    settings.feedback = 121; assert(!fx.applySettings(settings));
    settings = fx.settings(); settings.enabledMask = 0x80; assert(!fx.applySettings(settings));
    assert(!fx.setMix(static_cast<MasterEffectType>(99), 1));
    return 0;
}
