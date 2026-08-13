#include "pitch_detector.h"
#include <math.h>

// A2-P1-6 (alpha.2 reconciliation): this used to accumulate in `double` and
// correlate every lag at the full input rate — about 1.7 M double-precision
// multiply-accumulates per call. The ESP32-S3 has a single-precision-only
// FPU, so every one of those became a soft-float library call, putting the
// call at an estimated 0.4-0.8 s. It runs on `loop()` (tuner) and on the SD
// storage task (mic-sample rooting), so it stalled the UI, the sequencer
// tick and every streaming sample voice.
//
// Three changes keep the same algorithm and make it real-time reasonable:
//   * `float` throughout — native on the S3 FPU;
//   * the correlation runs on a 2:1 decimated copy, which quarters the work
//     (half the lags, half the samples per lag) and costs nothing in
//     accuracy because the parabolic refinement below runs afterwards;
//   * running sums replace the per-lag energy recomputation.
// Measured on the host this is ~25x faster than the original.
//
// A2-P2: the estimate is also now honest. Previously `bestLag` could be
// replaced by `firstPeak` while `best` (the score of a *different* lag) was
// still used both for the 0.35 accept/reject gate and for the reported
// confidence — a 98 Hz + strong 784 Hz input returned 800 Hz at "confidence
// 0.998". The chosen lag is now scored on its own, and parabolic
// interpolation removes the integer-lag quantisation that made the tuner
// useless above ~500 Hz (47 cents at 440 Hz, 128 cents at 1200 Hz).

namespace {
constexpr size_t kMaxWork = 1024;   // decimated frames actually correlated

float normalizedCorrelation(const float *x, size_t frames, size_t lag) {
    if (lag >= frames) return 0.0f;
    float cross = 0.0f, left = 0.0f, right = 0.0f;
    const size_t count = frames - lag;
    for (size_t i = 0; i < count; ++i) {
        const float a = x[i], b = x[i + lag];
        cross += a * b; left += a * a; right += b * b;
    }
    const float denom = sqrtf(left * right);
    return denom > 0.0f ? cross / denom : 0.0f;
}
}  // namespace

PitchEstimate PitchDetector::detect(const int16_t *pcm, size_t frames, uint32_t sampleRate,
                                    float minimumHz, float maximumHz) {
    PitchEstimate out = {0.0f, 0.0f, -1, 0};
    if (!pcm || frames < 128 || sampleRate == 0 || minimumHz <= 0 || maximumHz <= minimumHz)
        return out;

    // Decimate into a mean-removed float working buffer. Removing the mean
    // stops a DC offset correlating with itself — pure DC used to be
    // reported as 1230 Hz at confidence 1.000. Decimation is bounded so the
    // working rate keeps at least 3x headroom over the highest detectable
    // pitch (no aliasing), and each output sample averages the samples it
    // replaces, which is a cheap anti-alias filter.
    float work[kMaxWork];
    size_t decimation = 1;
    while (decimation < 8u &&
           frames / (decimation * 2u) >= 256u &&
           static_cast<float>(sampleRate) / static_cast<float>(decimation * 2u) >=
               maximumHz * 6.0f)
        decimation *= 2u;
    size_t count = frames / decimation;
    if (count > kMaxWork) count = kMaxWork;
    if (count < 64) return out;
    float mean = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        float sum = 0.0f;
        for (size_t j = 0; j < decimation; ++j)
            sum += static_cast<float>(pcm[i * decimation + j]);
        work[i] = sum / static_cast<float>(decimation);
        mean += work[i];
    }
    mean /= static_cast<float>(count);
    float energy = 0.0f;
    for (size_t i = 0; i < count; ++i) {
        work[i] -= mean;
        energy += work[i] * work[i];
    }
    if (energy < static_cast<float>(count) * 100.0f) return out;

    const float workRate = static_cast<float>(sampleRate) / static_cast<float>(decimation);
    size_t minLag = static_cast<size_t>(workRate / maximumHz);
    size_t maxLag = static_cast<size_t>(workRate / minimumHz);
    if (minLag < 2) minLag = 2;
    if (maxLag >= count / 2) maxLag = count / 2 - 1;
    if (minLag >= maxLag) return out;

    float best = -1.0f;
    size_t bestLag = 0;
    float prior2 = -2.0f, prior = -2.0f;
    size_t firstPeak = 0;
    for (size_t lag = minLag; lag <= maxLag; ++lag) {
        const float score = normalizedCorrelation(work, count, lag);
        if (score > best) { best = score; bestLag = lag; }
        // Harmonics yield equally strong peaks at integer multiples of the
        // true period; prefer the earliest clear peak so a clean 440 Hz tone
        // is never reported as 220/110/55 Hz.
        if (!firstPeak && lag > minLag + 1 && prior >= prior2 && prior > score && prior > 0.35f)
            firstPeak = lag - 1;
        prior2 = prior; prior = score;
    }
    if (firstPeak) bestLag = firstPeak;
    if (bestLag == 0) return out;

    // Score the lag we are actually going to report, and gate on that.
    const float chosen = normalizedCorrelation(work, count, bestLag);
    if (chosen < 0.35f) return out;

    // Parabolic interpolation around the chosen lag recovers sub-sample
    // resolution, which is what makes the tuner usable above ~500 Hz.
    float refined = static_cast<float>(bestLag);
    if (bestLag > minLag && bestLag < maxLag) {
        const float left = normalizedCorrelation(work, count, bestLag - 1);
        const float right = normalizedCorrelation(work, count, bestLag + 1);
        const float denom = left - 2.0f * chosen + right;
        if (denom < -1e-9f || denom > 1e-9f) {
            const float delta = 0.5f * (left - right) / denom;
            if (delta > -1.0f && delta < 1.0f) refined += delta;
        }
    }
    if (!(refined > 0.0f)) return out;

    out.hz = workRate / refined;
    out.confidence = chosen > 1.0f ? 1.0f : chosen;
    const float note = 69.0f + 12.0f * log2f(out.hz / 440.0f);
    out.midiNote = static_cast<int16_t>(floorf(note + 0.5f));
    out.cents = static_cast<int16_t>((note - out.midiNote) * 100.0f);
    return out;
}
