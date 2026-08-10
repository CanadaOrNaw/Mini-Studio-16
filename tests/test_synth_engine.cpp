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
    return 0;
}
