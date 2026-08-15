#pragma once
#include <stdint.h>

enum VocoderSource : uint8_t { VOCODER_LOOP1 = 0, VOCODER_MIC, VOCODER_LINE };
struct VocoderSettings {
    uint8_t enabled;
    uint8_t source;
    int8_t formantShift;
    uint8_t resonance;
    uint8_t attack;
    uint8_t release;
    uint8_t noise;
    uint8_t gate;
};

class Vocoder8Band {
public:
    Vocoder8Band();
    void reset();
    // A2-P1-1: static so project validation never has to construct one on
    // the loopTask stack (see MasterEffects::validate for the full story).
    static bool validate(const VocoderSettings &settings);
    bool applySettings(const VocoderSettings &settings);
    VocoderSettings settings() const;
    // Consume a pending change once per audio block. updateCoefficients()
    // runs 1 powf + 8 sinf, which must never happen mid-sample-loop.
    void syncSettings();
    // Band centre in Hz after the current formant shift; used by tests to
    // prove all eight bands stay distinct and ordered.
    float bandCenterHz(uint8_t band) const;
    int16_t process(int16_t carrier, int16_t modulator);
private:
    struct Band { float cLow, cBand, mLow, mBand, envelope, coefficient, centerHz; };
    Band bands_[8];
    VocoderSettings active_;
    volatile VocoderSettings pending_;
    volatile uint32_t sequence_;
    uint32_t appliedSequence_;
    uint32_t noiseState_;
    void updateCoefficients();
    void syncPending();
};
