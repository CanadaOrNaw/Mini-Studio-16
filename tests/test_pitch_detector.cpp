#include "../pitch_detector.h"
#include <assert.h>
#include <math.h>
#include <stdint.h>

int main() {
    int16_t pcm[2048];
    for (size_t i = 0; i < 2048; ++i)
        pcm[i] = static_cast<int16_t>(sin(6.28318530718 * 440.0 * i / 22050.0) * 24000.0);
    PitchEstimate p = PitchDetector::detect(pcm, 2048, 22050);
    assert(p.hz > 430.0f && p.hz < 450.0f && p.midiNote == 69 && p.confidence > 0.9f);
    for (size_t i = 0; i < 2048; ++i) pcm[i] = 0;
    p = PitchDetector::detect(pcm, 2048, 22050);
    assert(p.hz == 0.0f && p.midiNote == -1);
    assert(PitchDetector::detect(nullptr, 0, 0).midiNote == -1);
    return 0;
}
