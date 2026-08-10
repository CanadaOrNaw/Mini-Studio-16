#include "synth_engine.h"

#include "wavetable.h"

#include <math.h>

namespace {
float clampf(float value, float minimum, float maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}
float phaseToUnit(uint32_t phase) {
    return static_cast<float>(phase >> 8) * (1.0f / 16777216.0f);
}

float softDrive(float sample, float amount) {
    const float gain = 1.0f + clampf(amount, 0.0f, 1.0f) * 4.0f;
    float value = sample * gain;
    if (value > 1.0f) value = 1.0f;
    else if (value < -1.0f) value = -1.0f;
    return (value - value * value * value * (1.0f / 3.0f)) * 1.5f;
}

float mgOscillator(const MgPlusPatch& patch, uint32_t phase, float pulseWidth) {
    const float unit = phaseToUnit(phase);
    switch (patch.oscMode) {
        case OSC_SAW: return 2.0f * unit - 1.0f;
        case OSC_SQR: return unit < pulseWidth ? 0.7f : -0.7f;
        case OSC_TRI: return unit < 0.5f ? 4.0f * unit - 1.0f : 3.0f - 4.0f * unit;
        case OSC_SIN: return synthSine(phase);
        case OSC_WT: return wavetableRead(patch.wavetable, unit);
        default: return 0.0f;
    }
}

float fmOperator(FmOperatorState& state, const FmOperatorPatch& patch,
                 float modulationCycles) {
    const float envelope = state.envelope.render(patch.envelope);
    const float sample = synthSine(state.phase, modulationCycles) * envelope * patch.level;
    state.phase += state.increment;
    return sample;
}

float modulationCycles(float sample, const FmPatch& patch) {
    return sample * patch.modulationIndex * 0.159154943f;
}

float clampOutput(float sample) {
    if (sample > 1.0f) return 1.0f;
    if (sample < -1.0f) return -1.0f;
    return sample;
}
}  // namespace

void MgPlusPatch::init() {
    oscMode = OSC_SAW;
    wavetable = 0;
    filterMode = SYNTH_FILTER_LP;
    lfoDestination = SYNTH_LFO_NONE;
    cutoff = 0.35f;
    resonance = 0.35f;
    filterEnvAmount = 0.45f;
    pulseWidth = 0.50f;
    subLevel = 0.0f;
    lfoRate = 2.0f;
    lfoDepth = 0.0f;
    velocityAmp = 0.65f;
    velocityFilter = 0.25f;
    drive = 0.0f;
    volume = 0.70f;
    ampEnvelope.set(5, 180, 0.70f, 220);
    filterEnvelope.set(2, 240, 0.0f, 180);
}

void MgPlusVoice::init() {
    phase = subPhase = lfoPhase = 0;
    frequency = targetFrequency = 0.0f;
    filterLP = filterBP = 0.0f;
    velocity = 1.0f;
    note = 0xFF;
    accent = slide = active = false;
    ampEnvelope.init();
    filterEnvelope.init();
}

void MgPlusVoice::noteOn(float newFrequency, uint8_t midiNote, uint8_t midiVelocity,
                         bool accented, bool legato, const MgPlusPatch& patch) {
    targetFrequency = newFrequency;
    note = midiNote;
    velocity = clampf(static_cast<float>(midiVelocity) / 127.0f, 0.0f, 1.0f);
    accent = accented;
    if (legato && active) {
        slide = true;
        filterEnvelope.noteOn(patch.filterEnvelope, true);
    } else {
        slide = false;
        frequency = newFrequency;
        ampEnvelope.noteOn(patch.ampEnvelope, true);
        filterEnvelope.noteOn(patch.filterEnvelope, true);
    }
    active = true;
}

void MgPlusVoice::noteOff(const MgPlusPatch& patch) {
    ampEnvelope.noteOff(patch.ampEnvelope);
    filterEnvelope.noteOff(patch.filterEnvelope);
}

float MgPlusVoice::render(const MgPlusPatch& patch) {
    if (!active) return 0.0f;
    if (slide) frequency += (targetFrequency - frequency) * 0.005f;
    else frequency = targetFrequency;

    const float lfo = synthSine(lfoPhase);
    lfoPhase += synthPhaseIncrement(patch.lfoRate);
    float pitchScale = 1.0f;
    if (patch.lfoDestination == SYNTH_LFO_PITCH)
        pitchScale += lfo * patch.lfoDepth * 0.02f;
    const uint32_t increment = synthPhaseIncrement(frequency * pitchScale);
    phase += increment;
    subPhase += increment >> 1;

    float pulseWidth = patch.pulseWidth;
    if (patch.lfoDestination == SYNTH_LFO_PWM)
        pulseWidth += lfo * patch.lfoDepth * 0.40f;
    pulseWidth = clampf(pulseWidth, 0.05f, 0.95f);
    float oscillator = mgOscillator(patch, phase, pulseWidth);
    if (patch.subLevel > 0.0f)
        oscillator += (phaseToUnit(subPhase) < 0.5f ? 0.7f : -0.7f) * patch.subLevel;
    oscillator *= 1.0f / (1.0f + patch.subLevel * 0.7f);
    oscillator = softDrive(oscillator, patch.drive);

    const float amp = ampEnvelope.render(patch.ampEnvelope);
    const float filterEnv = filterEnvelope.render(patch.filterEnvelope);
    if (!ampEnvelope.active()) {
        active = false;
        note = 0xFF;
        return 0.0f;
    }

    float cutoff = patch.cutoff + filterEnv * patch.filterEnvAmount;
    cutoff += velocity * patch.velocityFilter * 0.20f;
    if (patch.lfoDestination == SYNTH_LFO_FILTER)
        cutoff += lfo * patch.lfoDepth * 0.25f;
    if (accent) cutoff += 0.12f;
    cutoff = clampf(cutoff, 0.03f, 0.80f);
    const float f = clampf(2.0f * synthSine(0, cutoff * 0.5f), 0.0f, 0.85f);
    const float q = clampf(1.0f - patch.resonance * 0.85f, 0.10f, 1.0f);
    filterLP += f * filterBP;
    const float high = oscillator - filterLP - q * filterBP;
    filterBP += f * high;
    filterLP = clampf(filterLP, -3.0f, 3.0f);
    filterBP = clampf(filterBP, -3.0f, 3.0f);

    float filtered = filterLP;
    if (patch.filterMode == SYNTH_FILTER_BP) filtered = filterBP;
    else if (patch.filterMode == SYNTH_FILTER_HP) filtered = high;
    float velocityGain = (1.0f - patch.velocityAmp) + velocity * patch.velocityAmp;
    float output = filtered * amp * velocityGain * patch.volume;
    if (patch.lfoDestination == SYNTH_LFO_AMP)
        output *= clampf(1.0f + lfo * patch.lfoDepth * 0.5f, 0.0f, 1.5f);
    if (accent) output *= 1.2f;
    return clampOutput(output);
}

void FmOperatorPatch::init(float frequencyRatio, float outputLevel) {
    ratio = frequencyRatio;
    level = outputLevel;
    envelope.set(3, 300, 0.65f, 350);
}

void FmPatch::init() {
    algorithm = 0;
    feedback = 0.10f;
    modulationIndex = 2.0f;
    volume = 0.65f;
    operators[0].init(1.0f, 1.0f);
    operators[1].init(2.0f, 0.75f);
    operators[2].init(1.0f, 0.55f);
    operators[3].init(3.0f, 0.35f);
}

void FmVoice::init() {
    for (uint8_t index = 0; index < 4; ++index) {
        operators[index].phase = 0;
        operators[index].increment = 0;
        operators[index].envelope.init();
    }
    feedbackSample = 0.0f;
    velocity = 1.0f;
    note = 0xFF;
    active = false;
}

void FmVoice::noteOn(float frequency, uint8_t midiNote, uint8_t midiVelocity,
                     const FmPatch& patch) {
    note = midiNote;
    velocity = clampf(static_cast<float>(midiVelocity) / 127.0f, 0.0f, 1.0f);
    feedbackSample = 0.0f;
    for (uint8_t index = 0; index < 4; ++index) {
        operators[index].phase = 0;
        operators[index].increment = synthPhaseIncrement(
            frequency * clampf(patch.operators[index].ratio, 0.25f, 16.0f));
        operators[index].envelope.noteOn(patch.operators[index].envelope, true);
    }
    active = true;
}

void FmVoice::noteOff(const FmPatch& patch) {
    for (uint8_t index = 0; index < 4; ++index)
        operators[index].envelope.noteOff(patch.operators[index].envelope);
}

float FmVoice::render(const FmPatch& patch) {
    if (!active) return 0.0f;
    const float feedbackCycles = clampf(feedbackSample, -1.0f, 1.0f) *
                                 clampf(patch.feedback, 0.0f, 1.0f) * 0.25f;
    const float op3 = fmOperator(operators[3], patch.operators[3], feedbackCycles);
    feedbackSample = op3;
    float op2 = 0.0f, op1 = 0.0f, op0 = 0.0f, output = 0.0f;
    switch (patch.algorithm < 8 ? patch.algorithm : 0) {
        case 0:  // 4 -> 3 -> 2 -> 1
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], modulationCycles(op2, patch));
            output = fmOperator(operators[0], patch.operators[0], modulationCycles(op1, patch));
            break;
        case 1:  // (4 + 3) -> 2 -> 1
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], modulationCycles(op3 + op2, patch));
            output = fmOperator(operators[0], patch.operators[0], modulationCycles(op1, patch));
            break;
        case 2:  // 4 -> 3, 2 -> 1 (two carriers)
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], 0.0f);
            op0 = fmOperator(operators[0], patch.operators[0], modulationCycles(op1, patch));
            output = (op0 + op2) * 0.70f;
            break;
        case 3:  // 4 -> 3 -> 2 plus operator 1
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], modulationCycles(op2, patch));
            op0 = fmOperator(operators[0], patch.operators[0], 0.0f);
            output = (op0 + op1) * 0.70f;
            break;
        case 4:  // 4 -> 3 plus operators 2 and 1
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], 0.0f);
            op0 = fmOperator(operators[0], patch.operators[0], 0.0f);
            output = (op0 + op1 + op2) * 0.48f;
            break;
        case 5:  // operator 4 modulates three carriers
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], modulationCycles(op3, patch));
            op0 = fmOperator(operators[0], patch.operators[0], modulationCycles(op3, patch));
            output = (op0 + op1 + op2) * 0.48f;
            break;
        case 6:  // 4 -> 3 -> two carriers
            op2 = fmOperator(operators[2], patch.operators[2], modulationCycles(op3, patch));
            op1 = fmOperator(operators[1], patch.operators[1], modulationCycles(op2, patch));
            op0 = fmOperator(operators[0], patch.operators[0], modulationCycles(op2, patch));
            output = (op0 + op1) * 0.70f;
            break;
        case 7:  // four additive carriers
            op2 = fmOperator(operators[2], patch.operators[2], 0.0f);
            op1 = fmOperator(operators[1], patch.operators[1], 0.0f);
            op0 = fmOperator(operators[0], patch.operators[0], 0.0f);
            output = (op0 + op1 + op2 + op3) * 0.36f;
            break;
    }
    bool anyActive = false;
    for (uint8_t index = 0; index < 4; ++index)
        anyActive = anyActive || operators[index].envelope.active();
    if (!anyActive) {
        active = false;
        note = 0xFF;
        return 0.0f;
    }
    return clampOutput(output * patch.volume * velocity);
}

float FmVoice::level() const {
    float highest = 0.0f;
    for (uint8_t index = 0; index < 4; ++index)
        if (operators[index].envelope.value > highest)
            highest = operators[index].envelope.value;
    return highest;
}

void SynthTrack::init() {
    for (int index = 0; index < MAX_POLY; ++index) {
        v[index].init();
        mgxVoices[index].init();
        fmVoices[index].init();
    }
    mgxPatch.init();
    fmPatch.init();
    engine = SYNTH_ENGINE_MG;
    voices = 1;
    rr = 0;
}

void SynthTrack::setEngine(SynthEngine selected) {
    if (selected >= SYNTH_ENGINE_COUNT || selected == engine) return;
    releaseAll();
    for (int index = 0; index < MAX_POLY; ++index) {
        v[index].active = false;
        mgxVoices[index].init();
        fmVoices[index].init();
    }
    engine = selected;
    rr = 0;
}

void SynthTrack::setVoices(uint8_t count) {
    if (count < 1) count = 1;
    if (count > MAX_POLY) count = MAX_POLY;
    voices = count;
    for (int index = count; index < MAX_POLY; ++index) {
        v[index].active = false;
        mgxVoices[index].init();
        fmVoices[index].init();
    }
    if (voices > 1) rr %= voices;
}

void SynthTrack::noteOn(float frequency, bool accented, bool legato) {
    noteOn(frequency, accented, legato, 0xFF, accented ? 127 : 96);
}

void SynthTrack::noteOn(float frequency, bool accented, bool legato,
                        uint8_t midiNote, uint8_t velocityValue) {
    if (voices <= 1) {
        if (engine == SYNTH_ENGINE_MG) v[0].noteOn(frequency, accented, legato);
        else if (engine == SYNTH_ENGINE_MGX)
            mgxVoices[0].noteOn(frequency, midiNote, velocityValue, accented,
                                legato, mgxPatch);
        else fmVoices[0].noteOn(frequency, midiNote, velocityValue, fmPatch);
        return;
    }

    int pick = -1;
    float quietest = 1.0e9f;
    for (int index = 0; index < voices; ++index) {
        bool isActive = engine == SYNTH_ENGINE_MG ? v[index].active :
                        engine == SYNTH_ENGINE_MGX ? mgxVoices[index].active :
                                                     fmVoices[index].active;
        if (!isActive) { pick = index; break; }
        const float levelValue = engine == SYNTH_ENGINE_MG ? v[index].ampEnv :
                                 engine == SYNTH_ENGINE_MGX ? mgxVoices[index].level() :
                                                              fmVoices[index].level();
        if (levelValue < quietest) { quietest = levelValue; pick = index; }
    }
    rr = static_cast<uint8_t>((pick + 1) % voices);
    if (engine == SYNTH_ENGINE_MG) v[pick].noteOn(frequency, accented, false);
    else if (engine == SYNTH_ENGINE_MGX)
        mgxVoices[pick].noteOn(frequency, midiNote, velocityValue, accented,
                               false, mgxPatch);
    else fmVoices[pick].noteOn(frequency, midiNote, velocityValue, fmPatch);
}

void SynthTrack::noteOff(uint8_t midiNote) {
    if (engine == SYNTH_ENGINE_MG) return;
    for (int index = 0; index < voices; ++index) {
        if (engine == SYNTH_ENGINE_MGX && mgxVoices[index].active &&
            (midiNote == 0xFF || mgxVoices[index].note == midiNote))
            mgxVoices[index].noteOff(mgxPatch);
        if (engine == SYNTH_ENGINE_FM4 && fmVoices[index].active &&
            (midiNote == 0xFF || fmVoices[index].note == midiNote))
            fmVoices[index].noteOff(fmPatch);
    }
}

void SynthTrack::releaseAll() { noteOff(0xFF); }

void SynthTrack::prepareStep(bool hasNotes, bool legato) {
    if (engine == SYNTH_ENGINE_MG) return;
    if (!hasNotes || !legato || voices > 1) releaseAll();
}

float SynthTrack::render() {
    float sample = 0.0f;
    if (voices <= 1) {
        if (engine == SYNTH_ENGINE_MG) return v[0].render();
        if (engine == SYNTH_ENGINE_MGX) return mgxVoices[0].render(mgxPatch);
        return fmVoices[0].render(fmPatch);
    }
    for (int index = 0; index < voices; ++index) {
        if (engine == SYNTH_ENGINE_MG) sample += v[index].render();
        else if (engine == SYNTH_ENGINE_MGX) sample += mgxVoices[index].render(mgxPatch);
        else sample += fmVoices[index].render(fmPatch);
    }
    return sample * (voices > 2 ? 0.62f : 0.75f);
}

void SynthTrack::setCutoff(float normalized) {
    normalized = clampf(normalized, 0.0f, 1.0f);
    if (engine == SYNTH_ENGINE_MG)
        forEach([normalized](SynthVoice& voice) { voice.fltCutoff = normalized; });
    else if (engine == SYNTH_ENGINE_MGX) mgxPatch.cutoff = normalized;
    else fmPatch.modulationIndex = normalized * 8.0f;
}

void SynthTrack::setResonance(float normalized) {
    normalized = clampf(normalized, 0.0f, 1.0f);
    if (engine == SYNTH_ENGINE_MG)
        forEach([normalized](SynthVoice& voice) { voice.fltReso = normalized; });
    else if (engine == SYNTH_ENGINE_MGX) mgxPatch.resonance = normalized;
    else fmPatch.feedback = normalized;
}

void SynthTrack::setVolume(float normalized) {
    normalized = clampf(normalized, 0.0f, 1.0f);
    if (engine == SYNTH_ENGINE_MG)
        forEach([normalized](SynthVoice& voice) { voice.volume = normalized; });
    else if (engine == SYNTH_ENGINE_MGX) mgxPatch.volume = normalized;
    else fmPatch.volume = normalized;
}

float SynthTrack::cutoff() const {
    if (engine == SYNTH_ENGINE_MG) return v[0].fltCutoff;
    if (engine == SYNTH_ENGINE_MGX) return mgxPatch.cutoff;
    return fmPatch.modulationIndex * 0.125f;
}

float SynthTrack::resonance() const {
    if (engine == SYNTH_ENGINE_MG) return v[0].fltReso;
    if (engine == SYNTH_ENGINE_MGX) return mgxPatch.resonance;
    return fmPatch.feedback;
}

float SynthTrack::volume() const {
    if (engine == SYNTH_ENGINE_MG) return v[0].volume;
    if (engine == SYNTH_ENGINE_MGX) return mgxPatch.volume;
    return fmPatch.volume;
}

const char* synthEngineName(SynthEngine value) {
    switch (value) {
        case SYNTH_ENGINE_MG: return "MG/303";
        case SYNTH_ENGINE_MGX: return "MGX";
        case SYNTH_ENGINE_FM4: return "FM4";
        default: return "UNKNOWN";
    }
}

const char* synthFilterModeName(SynthFilterMode value) {
    switch (value) {
        case SYNTH_FILTER_LP: return "LP";
        case SYNTH_FILTER_BP: return "BP";
        case SYNTH_FILTER_HP: return "HP";
        default: return "?";
    }
}

const char* synthLfoDestinationName(SynthLfoDestination value) {
    switch (value) {
        case SYNTH_LFO_NONE: return "OFF";
        case SYNTH_LFO_PITCH: return "PITCH";
        case SYNTH_LFO_FILTER: return "FILTER";
        case SYNTH_LFO_PWM: return "PWM";
        case SYNTH_LFO_AMP: return "AMP";
        default: return "?";
    }
}
