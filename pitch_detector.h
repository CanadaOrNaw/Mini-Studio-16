#pragma once
#include <stddef.h>
#include <stdint.h>

struct PitchEstimate {
    float hz;
    float confidence;
    int16_t midiNote;
    int16_t cents;
};

class PitchDetector {
public:
    static PitchEstimate detect(const int16_t *pcm, size_t frames, uint32_t sampleRate,
                                float minimumHz = 50.0f, float maximumHz = 1200.0f);
};
