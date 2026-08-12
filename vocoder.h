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
    bool applySettings(const VocoderSettings &settings);
    VocoderSettings settings() const;
    int16_t process(int16_t carrier, int16_t modulator);
private:
    struct Band { float cLow, cBand, mLow, mBand, envelope, coefficient; };
    Band bands_[8];
    VocoderSettings active_;
    volatile VocoderSettings pending_;
    volatile uint32_t sequence_;
    uint32_t appliedSequence_;
    uint32_t noiseState_;
    void updateCoefficients();
    void syncPending();
};
