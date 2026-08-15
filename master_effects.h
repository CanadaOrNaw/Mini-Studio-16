#pragma once
#include <stdint.h>

enum MasterEffectType : uint8_t {
    MASTER_REVERB = 0, MASTER_DELAY, MASTER_CHORUS, MASTER_FLANGER,
    MASTER_TREMOLO, MASTER_VIBRATO, MASTER_FILTER, MASTER_EFFECT_COUNT
};

struct MasterEffectsSettings {
    uint8_t enabledMask;
    uint8_t mix[MASTER_EFFECT_COUNT]; // 0..127
    uint8_t feedback;                 // 0..120
    uint8_t rate;                     // 1..127
    uint8_t filter;                   // 1..127
};

class MasterEffects {
public:
    MasterEffects();
    void reset();
    bool setEnabled(MasterEffectType effect, bool enabled);
    bool enabled(MasterEffectType effect) const;
    bool setMix(MasterEffectType effect, uint8_t value);
    bool setFeedback(uint8_t value);
    bool setRate(uint8_t value);
    bool setFilter(uint8_t value);
    MasterEffectsSettings settings() const;
    // A2-P1-1 (alpha.2 reconciliation): range checking MUST NOT require an
    // instance. performanceProjectValidate() used to construct a temporary
    // MasterEffects purely to call applySettings(), putting 8,236 bytes on
    // the 8,192-byte Arduino loopTask stack and making every saved v9
    // project unloadable on hardware. The check is pure, so it is static.
    static bool validate(const MasterEffectsSettings &settings);
    bool applySettings(const MasterEffectsSettings &settings);
    // Consume a pending settings change. Call once per audio block, before
    // the per-sample loop, so the seqlock read never lands mid-block.
    void syncSettings();
    int16_t process(int16_t input);
private:
    int16_t delay_[4096];
    uint16_t write_;
    uint32_t phase_;
    int32_t lowpass_;
    MasterEffectsSettings active_;
    volatile MasterEffectsSettings pending_;
    volatile uint32_t sequence_;
    uint32_t appliedSequence_;
    int16_t tap(uint16_t delay) const;
    static int16_t clamp(int32_t value);
    int16_t blend(int16_t dry, int16_t wet, uint8_t mix) const;
    void syncPending();
};
