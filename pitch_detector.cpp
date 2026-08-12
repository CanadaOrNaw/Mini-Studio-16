#include "pitch_detector.h"
#include <math.h>

PitchEstimate PitchDetector::detect(const int16_t *pcm, size_t frames, uint32_t sampleRate,
                                    float minimumHz, float maximumHz) {
    PitchEstimate out = {0.0f, 0.0f, -1, 0};
    if (!pcm || frames < 128 || sampleRate == 0 || minimumHz <= 0 || maximumHz <= minimumHz)
        return out;
    size_t minLag = static_cast<size_t>(sampleRate / maximumHz);
    size_t maxLag = static_cast<size_t>(sampleRate / minimumHz);
    if (minLag < 2) minLag = 2;
    if (maxLag >= frames / 2) maxLag = frames / 2 - 1;
    if (minLag >= maxLag) return out;
    double energy = 0.0;
    for (size_t i = 0; i < frames; ++i) energy += static_cast<double>(pcm[i]) * pcm[i];
    if (energy < frames * 100.0) return out;
    double best = -1.0;
    size_t bestLag = 0;
    double prior2 = -2.0, prior = -2.0;
    size_t firstPeak = 0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        double cross = 0.0, left = 0.0, right = 0.0;
        for (size_t i = 0; i + lag < frames; ++i) {
            const double a = pcm[i], b = pcm[i + lag];
            cross += a * b; left += a * a; right += b * b;
        }
        const double denom = sqrt(left * right);
        const double score = denom > 0.0 ? cross / denom : 0.0;
        if (score > best) { best = score; bestLag = lag; }
        if (!firstPeak && lag > minLag + 1 && prior >= prior2 && prior > score && prior > 0.35)
            firstPeak = lag - 1;
        prior2 = prior; prior = score;
    }
    // Harmonics yield equally strong peaks at integer multiples of the true
    // period. Select the earliest peak within 98% of the global maximum so a
    // clean 440 Hz tone cannot be misreported as 220/110/55 Hz.
    if (firstPeak) {
        bestLag = firstPeak;
    } else if (best > 0.0) {
        for (size_t lag = minLag; lag < bestLag; ++lag) {
            double cross = 0.0, left = 0.0, right = 0.0;
            for (size_t i = 0; i + lag < frames; ++i) {
                const double a = pcm[i], b = pcm[i + lag];
                cross += a * b; left += a * a; right += b * b;
            }
            const double denom = sqrt(left * right);
            if (denom > 0.0 && cross / denom >= best * 0.98) { bestLag = lag; break; }
        }
    }
    if (bestLag == 0 || best < 0.35) return out;
    out.hz = static_cast<float>(sampleRate) / bestLag;
    out.confidence = static_cast<float>(best > 1.0 ? 1.0 : best);
    const float note = 69.0f + 12.0f * log2f(out.hz / 440.0f);
    out.midiNote = static_cast<int16_t>(floorf(note + 0.5f));
    out.cents = static_cast<int16_t>((note - out.midiNote) * 100.0f);
    return out;
}
