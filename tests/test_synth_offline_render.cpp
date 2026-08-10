#include "../synth_engine.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <vector>

namespace {
struct RenderStats {
    uint32_t hash;
    double rms;
    float peak;
    double sidebandEnergy;
};

RenderStats renderPatch(float modulationIndex, std::vector<float>& output) {
    SynthTrack track = {};
    track.init();
    track.setEngine(SYNTH_ENGINE_FM4);
    track.fmPatch.algorithm = 0;
    track.fmPatch.feedback = 0.0f;
    track.fmPatch.modulationIndex = modulationIndex;
    track.fmPatch.volume = 0.65f;
    for (uint8_t index = 0; index < 4; ++index) {
        track.fmPatch.operators[index].envelope.set(0, 0, 1.0f, 100);
        track.fmPatch.operators[index].level = index == 0 ? 1.0f : 0.8f;
        track.fmPatch.operators[index].ratio = index == 0 ? 1.0f : 2.0f + index;
    }
    track.noteOn(220.0f, false, false, 57, 127);

    RenderStats stats = {2166136261u, 0.0, 0.0f, 0.0};
    double sumSquares = 0.0;
    for (size_t frame = 0; frame < output.size(); ++frame) {
        const float sample = track.render();
        assert(isfinite(sample));
        assert(sample >= -1.00001f && sample <= 1.00001f);
        output[frame] = sample;
        const float absolute = fabsf(sample);
        if (absolute > stats.peak) stats.peak = absolute;
        sumSquares += static_cast<double>(sample) * sample;
        const int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
        stats.hash ^= static_cast<uint16_t>(pcm);
        stats.hash *= 16777619u;
    }
    stats.rms = sqrt(sumSquares / static_cast<double>(output.size()));

    const double twoPi = 6.283185307179586;
    for (int bin = 1; bin <= 200; ++bin) {
        if (bin >= 42 && bin <= 46) continue;
        double real = 0.0;
        double imaginary = 0.0;
        for (size_t frame = 0; frame < output.size(); ++frame) {
            const double angle = twoPi * static_cast<double>(bin) * frame /
                                 static_cast<double>(output.size());
            real += output[frame] * cos(angle);
            imaginary -= output[frame] * sin(angle);
        }
        stats.sidebandEnergy += real * real + imaginary * imaginary;
    }
    return stats;
}
}  // namespace

int main() {
    std::vector<float> plain(4410);
    std::vector<float> modulated(4410);
    std::vector<float> repeated(4410);
    const RenderStats base = renderPatch(0.0f, plain);
    const RenderStats fm = renderPatch(5.0f, modulated);
    const RenderStats fmAgain = renderPatch(5.0f, repeated);

    assert(base.peak > 0.1f && base.peak <= 1.0f);
    assert(fm.peak > 0.1f && fm.peak <= 1.0f);
    assert(base.rms > 0.01 && fm.rms > 0.01);
    assert(base.hash != fm.hash);
    assert(base.hash == 0xac9acdedu);
    assert(fm.hash == 0x96bd5991u);
    assert(fm.hash == fmAgain.hash);
    assert(fabs(fm.rms - fmAgain.rms) < 1.0e-12);
    assert(fm.sidebandEnergy == fmAgain.sidebandEnergy);
    assert(fm.sidebandEnergy > base.sidebandEnergy * 100.0);

    double waveformDifference = 0.0;
    for (size_t frame = 0; frame < plain.size(); ++frame)
        waveformDifference += fabs(static_cast<double>(plain[frame] - modulated[frame]));
    assert(waveformDifference > 100.0);

    printf("offline-fm base_hash=%08x fm_hash=%08x base_rms=%.9f fm_rms=%.9f "
           "base_sidebands=%.3f fm_sidebands=%.3f\n",
           base.hash, fm.hash, base.rms, fm.rms,
           base.sidebandEnergy, fm.sidebandEnergy);
    return 0;
}
