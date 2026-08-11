#include "synth_project.h"

namespace {
uint16_t toQ15(float value) {
    if (value < 0.0f) value = 0.0f;
    if (value > 1.0f) value = 1.0f;
    return static_cast<uint16_t>(value * 32767.0f + 0.5f);
}

float fromQ15(uint16_t value) {
    return static_cast<float>(value) * (1.0f / 32767.0f);
}

uint16_t toCent(float value) {
    if (value < 0.0f) value = 0.0f;
    const float encoded = value * 100.0f + 0.5f;
    return static_cast<uint16_t>(encoded > 65535.0f ? 65535.0f : encoded);
}

void encodeAdsr(const SynthAdsrParams& input, SaveSynthAdsr& output) {
    output.attackMs = input.attackMs;
    output.decayMs = input.decayMs;
    output.releaseMs = input.releaseMs;
    output.sustainQ15 = toQ15(input.sustain);
}

void decodeAdsr(const SaveSynthAdsr& input, SynthAdsrParams& output) {
    output.attackMs = input.attackMs;
    output.decayMs = input.decayMs;
    output.releaseMs = input.releaseMs;
    output.sustain = fromQ15(input.sustainQ15);
}

bool validAdsr(const SaveSynthAdsr& envelope) {
    return envelope.attackMs <= 5000 && envelope.decayMs <= 5000 &&
           envelope.releaseMs <= 5000 && envelope.sustainQ15 <= 32767;
}
}  // namespace

void synthProjectEncode(const SynthTrack& track, SaveSynthEngineState& output) {
    output.engine = track.engine;
    const MgPlusPatch& mgx = track.mgxPatch;
    output.mgx.oscMode = mgx.oscMode;
    output.mgx.wavetable = mgx.wavetable;
    output.mgx.filterMode = mgx.filterMode;
    output.mgx.lfoDestination = mgx.lfoDestination;
    output.mgx.cutoffQ15 = toQ15(mgx.cutoff);
    output.mgx.resonanceQ15 = toQ15(mgx.resonance);
    output.mgx.filterEnvQ15 = toQ15(mgx.filterEnvAmount);
    output.mgx.pulseWidthQ15 = toQ15(mgx.pulseWidth);
    output.mgx.subLevelQ15 = toQ15(mgx.subLevel);
    output.mgx.lfoRateCent = toCent(mgx.lfoRate);
    output.mgx.lfoDepthQ15 = toQ15(mgx.lfoDepth);
    output.mgx.velocityAmpQ15 = toQ15(mgx.velocityAmp);
    output.mgx.velocityFilterQ15 = toQ15(mgx.velocityFilter);
    output.mgx.driveQ15 = toQ15(mgx.drive);
    output.mgx.volumeQ15 = toQ15(mgx.volume);
    encodeAdsr(mgx.ampEnvelope, output.mgx.ampEnvelope);
    encodeAdsr(mgx.filterEnvelope, output.mgx.filterEnvelope);

    const FmPatch& fm = track.fmPatch;
    output.fm.algorithm = fm.algorithm;
    output.fm.feedbackQ15 = toQ15(fm.feedback);
    output.fm.modulationIndexCent = toCent(fm.modulationIndex);
    output.fm.volumeQ15 = toQ15(fm.volume);
    for (uint8_t index = 0; index < 4; ++index) {
        output.fm.operators[index].ratioCent = toCent(fm.operators[index].ratio);
        output.fm.operators[index].levelQ15 = toQ15(fm.operators[index].level);
        encodeAdsr(fm.operators[index].envelope,
                   output.fm.operators[index].envelope);
    }
}

bool synthProjectValidate(const SaveSynthEngineState& input) {
    if (input.engine >= SYNTH_ENGINE_COUNT || input.mgx.oscMode >= OSC_COUNT ||
        input.mgx.wavetable >= NUM_WT_TOTAL ||
        input.mgx.filterMode >= SYNTH_FILTER_COUNT ||
        input.mgx.lfoDestination >= SYNTH_LFO_COUNT ||
        input.mgx.cutoffQ15 > 32767 || input.mgx.resonanceQ15 > 32767 ||
        input.mgx.filterEnvQ15 > 32767 || input.mgx.pulseWidthQ15 < 1638 ||
        input.mgx.pulseWidthQ15 > 31129 || input.mgx.subLevelQ15 > 32767 ||
        input.mgx.lfoRateCent < 5 || input.mgx.lfoRateCent > 2000 ||
        input.mgx.lfoDepthQ15 > 32767 || input.mgx.velocityAmpQ15 > 32767 ||
        input.mgx.velocityFilterQ15 > 32767 || input.mgx.driveQ15 > 32767 ||
        input.mgx.volumeQ15 > 32767 || !validAdsr(input.mgx.ampEnvelope) ||
        !validAdsr(input.mgx.filterEnvelope) || input.fm.algorithm > 7 ||
        input.fm.feedbackQ15 > 32767 || input.fm.modulationIndexCent > 800 ||
        input.fm.volumeQ15 > 32767)
        return false;
    for (uint8_t index = 0; index < 4; ++index) {
        const SaveFmOperatorPatch& op = input.fm.operators[index];
        if (op.ratioCent < 25 || op.ratioCent > 1600 || op.levelQ15 > 32767 ||
            !validAdsr(op.envelope)) return false;
    }
    return true;
}

bool synthProjectDecode(const SaveSynthEngineState& input, SynthTrack& track) {
    if (!synthProjectValidate(input)) return false;
    MgPlusPatch& mgx = track.mgxPatch;
    mgx.oscMode = static_cast<OscMode>(input.mgx.oscMode);
    mgx.wavetable = input.mgx.wavetable;
    mgx.filterMode = static_cast<SynthFilterMode>(input.mgx.filterMode);
    mgx.lfoDestination = static_cast<SynthLfoDestination>(input.mgx.lfoDestination);
    mgx.cutoff = fromQ15(input.mgx.cutoffQ15);
    mgx.resonance = fromQ15(input.mgx.resonanceQ15);
    mgx.filterEnvAmount = fromQ15(input.mgx.filterEnvQ15);
    mgx.pulseWidth = fromQ15(input.mgx.pulseWidthQ15);
    mgx.subLevel = fromQ15(input.mgx.subLevelQ15);
    mgx.lfoRate = static_cast<float>(input.mgx.lfoRateCent) * 0.01f;
    mgx.lfoDepth = fromQ15(input.mgx.lfoDepthQ15);
    mgx.velocityAmp = fromQ15(input.mgx.velocityAmpQ15);
    mgx.velocityFilter = fromQ15(input.mgx.velocityFilterQ15);
    mgx.drive = fromQ15(input.mgx.driveQ15);
    mgx.volume = fromQ15(input.mgx.volumeQ15);
    decodeAdsr(input.mgx.ampEnvelope, mgx.ampEnvelope);
    decodeAdsr(input.mgx.filterEnvelope, mgx.filterEnvelope);

    FmPatch& fm = track.fmPatch;
    fm.algorithm = input.fm.algorithm;
    fm.feedback = fromQ15(input.fm.feedbackQ15);
    fm.modulationIndex = static_cast<float>(input.fm.modulationIndexCent) * 0.01f;
    fm.volume = fromQ15(input.fm.volumeQ15);
    for (uint8_t index = 0; index < 4; ++index) {
        fm.operators[index].ratio =
            static_cast<float>(input.fm.operators[index].ratioCent) * 0.01f;
        fm.operators[index].level = fromQ15(input.fm.operators[index].levelQ15);
        decodeAdsr(input.fm.operators[index].envelope,
                   fm.operators[index].envelope);
    }
    // P2-8: project loads run on the storage/main task while the audio task
    // renders; the engine switch is applied at the next audio block. The
    // patches written above are engine-specific structs, so writing them
    // before the switch lands is safe.
    track.requestEngine(static_cast<SynthEngine>(input.engine));
    return true;
}

void synthProjectMigrateLegacy(SynthTrack& track) {
    track.requestEngine(SYNTH_ENGINE_MG);
}

