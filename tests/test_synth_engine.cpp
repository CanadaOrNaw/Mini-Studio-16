#include "../synth_engine.h"
#include "../synth_parameters.h"

#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>

namespace {
uint32_t pcmHash(SynthTrack& track, uint32_t frames) {
    uint32_t hash = 2166136261u;
    for (uint32_t frame = 0; frame < frames; ++frame) {
        const float sample = track.render();
        assert(isfinite(sample));
        assert(sample >= -1.00001f && sample <= 1.00001f);
        const int16_t pcm = static_cast<int16_t>(sample * 32767.0f);
        hash ^= static_cast<uint16_t>(pcm);
        hash *= 16777619u;
    }
    return hash;
}

void sustainedFm(FmPatch& patch) {
    for (uint8_t index = 0; index < 4; ++index)
        patch.operators[index].envelope.set(0, 0, 1.0f, 100);
}
}  // namespace

int main() {
    static_assert(sizeof(SynthTrack) < 1024,
                  "fixed per-track synth state exceeded the software RAM boundary");
    printf("synth-state legacy=%zu mgxVoice=%zu fmVoice=%zu track=%zu allTracks=%zu\n",
           sizeof(SynthVoice), sizeof(MgPlusVoice), sizeof(FmVoice),
           sizeof(SynthTrack), sizeof(SynthTrack) * NUM_SYNTHS);

    SynthAdsrParams envelopeParams = {};
    envelopeParams.set(10, 20, 0.25f, 30);
    SynthAdsr envelope = {};
    envelope.init();
    envelope.noteOn(envelopeParams);
    const uint32_t attackSamples = 10u * SAMPLE_RATE / 1000u;
    for (uint32_t index = 0; index <= attackSamples; ++index) envelope.render(envelopeParams);
    assert(envelope.value > 0.999f && envelope.stage == SYNTH_ENV_DECAY);
    const uint32_t decaySamples = 20u * SAMPLE_RATE / 1000u;
    for (uint32_t index = 0; index <= decaySamples; ++index) envelope.render(envelopeParams);
    assert(fabsf(envelope.value - 0.25f) < 0.002f);
    assert(envelope.stage == SYNTH_ENV_SUSTAIN);
    envelope.noteOff(envelopeParams);
    const uint32_t releaseSamples = 30u * SAMPLE_RATE / 1000u;
    for (uint32_t index = 0; index <= releaseSamples; ++index) envelope.render(envelopeParams);
    assert(!envelope.active() && envelope.value == 0.0f);

    FmPatch ratioPatch = {};
    ratioPatch.init();
    const float ratios[4] = {0.5f, 1.0f, 2.0f, 16.0f};
    for (uint8_t index = 0; index < 4; ++index)
        ratioPatch.operators[index].ratio = ratios[index];
    FmVoice ratioVoice = {};
    ratioVoice.init();
    ratioVoice.noteOn(220.0f, 57, 127, ratioPatch);
    for (uint8_t index = 0; index < 4; ++index)
        assert(ratioVoice.operators[index].increment ==
               synthPhaseIncrement(220.0f * ratios[index]));

    uint32_t algorithmHashes[8] = {};
    for (uint8_t algorithm = 0; algorithm < 8; ++algorithm) {
        SynthTrack track = {};
        track.init();
        track.setEngine(SYNTH_ENGINE_FM4);
        sustainedFm(track.fmPatch);
        track.fmPatch.algorithm = algorithm;
        track.fmPatch.modulationIndex = 4.25f;
        track.fmPatch.feedback = 0.30f;
        track.noteOn(220.0f, false, false, 57, 110);
        algorithmHashes[algorithm] = pcmHash(track, 2048);
        assert(algorithmHashes[algorithm] != 0u);
        for (uint8_t previous = 0; previous < algorithm; ++previous)
            assert(algorithmHashes[algorithm] != algorithmHashes[previous]);
    }

    SynthTrack first = {};
    SynthTrack second = {};
    first.init();
    second.init();
    first.setEngine(SYNTH_ENGINE_FM4);
    second.setEngine(SYNTH_ENGINE_FM4);
    sustainedFm(first.fmPatch);
    sustainedFm(second.fmPatch);
    first.fmPatch.algorithm = second.fmPatch.algorithm = 5;
    first.fmPatch.feedback = second.fmPatch.feedback = 0.85f;
    first.fmPatch.modulationIndex = second.fmPatch.modulationIndex = 7.5f;
    first.noteOn(261.6256f, false, false, 60, 127);
    second.noteOn(261.6256f, false, false, 60, 127);
    assert(pcmHash(first, 4096) == pcmHash(second, 4096));

    assert(synthSetParameter(first, SYNTH_PARAM_FM_FEEDBACK, 100));
    assert(!synthSetParameter(first, SYNTH_PARAM_FM_FEEDBACK, 101));
    first.noteOn(329.6276f, false, false, 64, 127);
    (void)pcmHash(first, 4096);

    SynthTrack switched = {};
    switched.init();
    switched.setVoices(3);
    switched.mgxPatch.cutoff = 0.61f;
    switched.setEngine(SYNTH_ENGINE_MGX);
    switched.noteOn(220.0f, false, false, 57, 100);
    assert(switched.mgxVoices[0].active && switched.mgxVoices[0].note == 57);
    switched.noteOn(261.6256f, false, false, 60, 100);
    switched.noteOn(329.6276f, false, false, 64, 100);
    assert(switched.mgxVoices[1].note == 60 && switched.mgxVoices[2].note == 64);
    (void)switched.render();
    switched.noteOff(57);
    assert(switched.mgxVoices[0].ampEnvelope.stage == SYNTH_ENV_RELEASE);
    switched.setEngine(SYNTH_ENGINE_FM4);
    assert(!switched.mgxVoices[0].active && !switched.fmVoices[0].active);
    switched.setEngine(SYNTH_ENGINE_MGX);
    assert(fabsf(switched.mgxPatch.cutoff - 0.61f) < 0.0001f);

    SynthTrack legacyTrack = {};
    legacyTrack.init();
    SynthVoice direct = {};
    direct.init();
    legacyTrack.v[0].oscMode = direct.oscMode = OSC_TRI;
    legacyTrack.v[0].fltCutoff = direct.fltCutoff = 0.32f;
    legacyTrack.v[0].fltReso = direct.fltReso = 0.64f;
    legacyTrack.v[0].fltEnvAmt = direct.fltEnvAmt = 0.27f;
    legacyTrack.v[0].volume = direct.volume = 0.73f;
    legacyTrack.noteOn(196.0f, true, false);
    direct.noteOn(196.0f, true, false);
    uint32_t legacyHash = 2166136261u;
    for (uint32_t frame = 0; frame < 4096; ++frame) {
        const float trackSample = legacyTrack.render();
        assert(trackSample == direct.render());
        legacyHash ^= static_cast<uint16_t>(
            static_cast<int16_t>(trackSample * 32767.0f));
        legacyHash *= 16777619u;
    }
    assert(legacyHash == 0xa202afdcu);
    printf("legacy-mg hash=%08x\n", legacyHash);

    SynthParameter parsed = SYNTH_PARAM_ENGINE;
    assert(synthParameterFromName("FM.OP4.RATIO", parsed));
    assert(parsed == SYNTH_PARAM_FM_OP4_RATIO);
    assert(!synthParameterFromName("fm.op5.ratio", parsed));
    const float oldRatio = switched.fmPatch.operators[3].ratio;
    assert(!synthSetParameter(switched, SYNTH_PARAM_FM_OP4_RATIO, 24));
    assert(!synthSetParameter(switched, SYNTH_PARAM_FM_OP4_RATIO, 1601));
    assert(switched.fmPatch.operators[3].ratio == oldRatio);
    assert(!synthSetParameter(switched, static_cast<SynthParameter>(255), 0));

    // P1-1 regression: the MGX filter must actually move with cutoff. With
    // the legacy 0.45 coefficient domain the 0.85 clamp engages near
    // cutoff ~0.31 (the same ~3.1 kHz ceiling the MG/303 engine has), so
    // probe inside the active region: renders must differ pairwise, and the
    // ratio of 20th-harmonic (2,200 Hz) to 3rd-harmonic (330 Hz) energy of
    // a 110 Hz saw must rise strictly as the low-pass opens. Before the
    // fix, everything from cutoff 0.14 upward rendered bit-identically.
    {
        float spectralRatio[3] = {0.0f, 0.0f, 0.0f};
        uint32_t hashes[3] = {0u, 0u, 0u};
        const float cutoffs[3] = {0.06f, 0.15f, 0.28f};
        static float window[2048];
        for (int c = 0; c < 3; ++c) {
            SynthTrack track = {};
            track.init();
            track.setEngine(SYNTH_ENGINE_MGX);
            track.mgxPatch.cutoff = cutoffs[c];
            track.mgxPatch.filterEnvAmount = 0.0f;   // isolate the knob
            track.mgxPatch.velocityFilter = 0.0f;
            track.noteOn(110.0f, false, false, 45, 100);
            uint32_t hash = 2166136261u;
            for (uint32_t frame = 0; frame < 2048; ++frame)
                (void)track.render();                // skip the attack
            for (uint32_t frame = 0; frame < 2048; ++frame) {
                window[frame] = track.render();
                hash ^= static_cast<uint16_t>(
                    static_cast<int16_t>(window[frame] * 32767.0f));
                hash *= 16777619u;
            }
            float magnitudes[2] = {0.0f, 0.0f};
            const float probes[2] = {330.0f, 2200.0f};
            for (int p = 0; p < 2; ++p) {
                float re = 0.0f, im = 0.0f;
                for (int i = 0; i < 2048; ++i) {
                    const float w = 2.0f * 3.14159265f * probes[p] *
                                    static_cast<float>(i) / 22050.0f;
                    re += window[i] * cosf(w);
                    im += window[i] * sinf(w);
                }
                magnitudes[p] = sqrtf(re * re + im * im);
            }
            spectralRatio[c] = magnitudes[1] / (magnitudes[0] + 1e-6f);
            hashes[c] = hash;
        }
        assert(hashes[0] != hashes[1] && hashes[1] != hashes[2] &&
               hashes[0] != hashes[2]);
        assert(spectralRatio[0] < spectralRatio[1] &&
               spectralRatio[1] < spectralRatio[2]);
        printf("mgx-cutoff spectral ratios %.4f %.4f %.4f\n",
               static_cast<double>(spectralRatio[0]),
               static_cast<double>(spectralRatio[1]),
               static_cast<double>(spectralRatio[2]));
    }

    // P2-9 regression: a live-held note must survive the sequencer's
    // per-step housekeeping; a sequenced voice on the same track must not.
    {
        SynthTrack track = {};
        track.init();
        track.setVoices(3);
        track.setEngine(SYNTH_ENGINE_MGX);
        track.noteOnLive(261.6256f, false, false, 60, 100);   // player holds C4
        const int seqVoice = track.noteOn(329.6276f, false, false, 64, 100);
        assert(track.mgxVoices[seqVoice].note == 64);
        for (int frame = 0; frame < 64; ++frame) (void)track.render();
        track.prepareStep(false, false);                       // empty step
        int liveVoice = -1;
        for (int i = 0; i < 3; ++i)
            if (track.mgxVoices[i].note == 60) liveVoice = i;
        assert(liveVoice >= 0);
        assert(track.mgxVoices[liveVoice].ampEnvelope.stage != SYNTH_ENV_RELEASE);
        assert(track.mgxVoices[seqVoice].ampEnvelope.stage == SYNTH_ENV_RELEASE);
        track.noteOff(60);                                     // player lets go
        assert(track.mgxVoices[liveVoice].ampEnvelope.stage == SYNTH_ENV_RELEASE);
        assert(track.liveMask == 0);
    }

    // P2-8 regression: engine switches requested cross-task are deferred to
    // applyPendingEngine (the audio task's block boundary) while
    // displayEngine() reflects the request immediately.
    {
        SynthTrack track = {};
        track.init();
        assert(track.engine == SYNTH_ENGINE_MG);
        track.requestEngine(SYNTH_ENGINE_FM4);
        assert(track.engine == SYNTH_ENGINE_MG);
        assert(track.displayEngine() == SYNTH_ENGINE_FM4);
        track.applyPendingEngine();
        assert(track.engine == SYNTH_ENGINE_FM4);
        assert(track.displayEngine() == SYNTH_ENGINE_FM4);
        track.applyPendingEngine();                            // idempotent
        assert(track.engine == SYNTH_ENGINE_FM4);
        assert(!synthSetParameter(track, SYNTH_PARAM_ENGINE, 99));
        assert(synthSetParameter(track, SYNTH_PARAM_ENGINE, SYNTH_ENGINE_MGX));
        int32_t shown = -1;
        assert(synthGetParameter(track, SYNTH_PARAM_ENGINE, shown));
        assert(shown == SYNTH_ENGINE_MGX);                     // display value
        assert(track.engine == SYNTH_ENGINE_FM4);              // not yet applied
        track.applyPendingEngine();
        assert(track.engine == SYNTH_ENGINE_MGX);
    }

    return 0;
}
