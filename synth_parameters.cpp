#include "synth_parameters.h"

#include "wavetable.h"

#include <string.h>

namespace {
float unit(int32_t value) { return static_cast<float>(value) * 0.01f; }
int32_t percent(float value) { return static_cast<int32_t>(value * 100.0f + 0.5f); }

bool setAdsr(SynthAdsrParams& envelope, SynthParameter parameter,
             SynthParameter attack, int32_t value) {
    const uint8_t offset = static_cast<uint8_t>(parameter - attack);
    if (offset > 3) return false;
    if (offset == 2) {
        if (value < 0 || value > 100) return false;
        envelope.sustain = unit(value);
    } else {
        if (value < 0 || value > 5000) return false;
        if (offset == 0) envelope.attackMs = static_cast<uint16_t>(value);
        else if (offset == 1) envelope.decayMs = static_cast<uint16_t>(value);
        else envelope.releaseMs = static_cast<uint16_t>(value);
    }
    return true;
}

bool getAdsr(const SynthAdsrParams& envelope, SynthParameter parameter,
             SynthParameter attack, int32_t& value) {
    const uint8_t offset = static_cast<uint8_t>(parameter - attack);
    if (offset > 3) return false;
    if (offset == 0) value = envelope.attackMs;
    else if (offset == 1) value = envelope.decayMs;
    else if (offset == 2) value = percent(envelope.sustain);
    else value = envelope.releaseMs;
    return true;
}

bool sameWord(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == 0 && *right == 0;
}

const char* const kNames[SYNTH_PARAM_COUNT] = {
    "engine", "voices", "volume",
    "mg.osc", "mg.wavetable", "mg.cutoff", "mg.resonance",
    "mg.filter_env", "mg.filter_decay", "mg.amp_decay",
    "mgx.osc", "mgx.wavetable", "mgx.filter_mode", "mgx.cutoff",
    "mgx.resonance", "mgx.filter_env", "mgx.pulse_width", "mgx.sub_level",
    "mgx.drive", "mgx.velocity_amp", "mgx.velocity_filter",
    "mgx.amp.attack", "mgx.amp.decay", "mgx.amp.sustain", "mgx.amp.release",
    "mgx.filter.attack", "mgx.filter.decay", "mgx.filter.sustain",
    "mgx.filter.release", "mgx.lfo.destination", "mgx.lfo.rate",
    "mgx.lfo.depth", "fm.algorithm", "fm.feedback", "fm.index",
    "fm.op1.ratio", "fm.op1.level", "fm.op1.attack", "fm.op1.decay",
    "fm.op1.sustain", "fm.op1.release",
    "fm.op2.ratio", "fm.op2.level", "fm.op2.attack", "fm.op2.decay",
    "fm.op2.sustain", "fm.op2.release",
    "fm.op3.ratio", "fm.op3.level", "fm.op3.attack", "fm.op3.decay",
    "fm.op3.sustain", "fm.op3.release",
    "fm.op4.ratio", "fm.op4.level", "fm.op4.attack", "fm.op4.decay",
    "fm.op4.sustain", "fm.op4.release",
};
}  // namespace

bool synthSetParameter(SynthTrack& track, SynthParameter parameter, int32_t value) {
    if (parameter >= SYNTH_PARAM_COUNT) return false;
    if (parameter == SYNTH_PARAM_ENGINE) {
        if (value < 0 || value >= SYNTH_ENGINE_COUNT) return false;
        // P2-8: parameter writes arrive from the UI/serial task while the
        // audio task renders; the switch is applied at the next block
        // boundary instead of re-initialising voices mid-render.
        track.requestEngine(static_cast<SynthEngine>(value));
        return true;
    }
    if (parameter == SYNTH_PARAM_VOICES) {
        if (value < 1 || value > MAX_POLY) return false;
        track.setVoices(static_cast<uint8_t>(value));
        return true;
    }
    if (parameter == SYNTH_PARAM_VOLUME) {
        if (value < 0 || value > 100) return false;
        track.setVolume(unit(value));
        return true;
    }

    SynthVoice& legacy = track.v[0];
    if (parameter == SYNTH_PARAM_MG_OSC) {
        if (value < 0 || value >= OSC_COUNT) return false;
        track.forEach([value](SynthVoice& voice) {
            voice.oscMode = static_cast<OscMode>(value);
        });
        return true;
    }
    if (parameter == SYNTH_PARAM_MG_WAVETABLE) {
        if (value < 0 || value >= NUM_WT_TOTAL) return false;
        track.forEach([value](SynthVoice& voice) {
            voice.wtIndex = static_cast<uint8_t>(value);
        });
        return true;
    }
    if (parameter >= SYNTH_PARAM_MG_CUTOFF && parameter <= SYNTH_PARAM_MG_FILTER_ENV) {
        if (value < 0 || value > 100) return false;
        const float normalized = unit(value);
        track.forEach([parameter, normalized](SynthVoice& voice) {
            if (parameter == SYNTH_PARAM_MG_CUTOFF) voice.fltCutoff = normalized;
            else if (parameter == SYNTH_PARAM_MG_RESONANCE) voice.fltReso = normalized;
            else voice.fltEnvAmt = normalized;
        });
        return true;
    }
    if (parameter == SYNTH_PARAM_MG_FILTER_DECAY ||
        parameter == SYNTH_PARAM_MG_AMP_DECAY) {
        if (value < 0 || value > 100) return false;
        track.forEach([parameter, value](SynthVoice& voice) {
            if (parameter == SYNTH_PARAM_MG_FILTER_DECAY)
                voice.filtDecRate = 0.9950f + static_cast<float>(value) * 0.0000495f;
            else voice.ampDecRate = 0.9990f + static_cast<float>(value) * 0.0000099f;
        });
        return true;
    }

    MgPlusPatch& mgx = track.mgxPatch;
    if (parameter == SYNTH_PARAM_MGX_OSC) {
        if (value < 0 || value >= OSC_COUNT) return false;
        mgx.oscMode = static_cast<OscMode>(value); return true;
    }
    if (parameter == SYNTH_PARAM_MGX_WAVETABLE) {
        if (value < 0 || value >= NUM_WT_TOTAL) return false;
        mgx.wavetable = static_cast<uint8_t>(value); return true;
    }
    if (parameter == SYNTH_PARAM_MGX_FILTER_MODE) {
        if (value < 0 || value >= SYNTH_FILTER_COUNT) return false;
        mgx.filterMode = static_cast<SynthFilterMode>(value); return true;
    }
    if (parameter == SYNTH_PARAM_MGX_LFO_DESTINATION) {
        if (value < 0 || value >= SYNTH_LFO_COUNT) return false;
        mgx.lfoDestination = static_cast<SynthLfoDestination>(value); return true;
    }
    if (parameter == SYNTH_PARAM_MGX_LFO_RATE) {
        if (value < 5 || value > 2000) return false;
        mgx.lfoRate = static_cast<float>(value) * 0.01f; return true;
    }
    if (parameter >= SYNTH_PARAM_MGX_AMP_ATTACK &&
        parameter <= SYNTH_PARAM_MGX_AMP_RELEASE)
        return setAdsr(mgx.ampEnvelope, parameter, SYNTH_PARAM_MGX_AMP_ATTACK, value);
    if (parameter >= SYNTH_PARAM_MGX_FILTER_ATTACK &&
        parameter <= SYNTH_PARAM_MGX_FILTER_RELEASE)
        return setAdsr(mgx.filterEnvelope, parameter, SYNTH_PARAM_MGX_FILTER_ATTACK, value);

    float* mgxUnit = nullptr;
    switch (parameter) {
        case SYNTH_PARAM_MGX_CUTOFF: mgxUnit = &mgx.cutoff; break;
        case SYNTH_PARAM_MGX_RESONANCE: mgxUnit = &mgx.resonance; break;
        case SYNTH_PARAM_MGX_FILTER_ENV: mgxUnit = &mgx.filterEnvAmount; break;
        case SYNTH_PARAM_MGX_PULSE_WIDTH: mgxUnit = &mgx.pulseWidth; break;
        case SYNTH_PARAM_MGX_SUB_LEVEL: mgxUnit = &mgx.subLevel; break;
        case SYNTH_PARAM_MGX_DRIVE: mgxUnit = &mgx.drive; break;
        case SYNTH_PARAM_MGX_VELOCITY_AMP: mgxUnit = &mgx.velocityAmp; break;
        case SYNTH_PARAM_MGX_VELOCITY_FILTER: mgxUnit = &mgx.velocityFilter; break;
        case SYNTH_PARAM_MGX_LFO_DEPTH: mgxUnit = &mgx.lfoDepth; break;
        default: break;
    }
    if (mgxUnit) {
        const int32_t minimum = parameter == SYNTH_PARAM_MGX_PULSE_WIDTH ? 5 : 0;
        const int32_t maximum = parameter == SYNTH_PARAM_MGX_PULSE_WIDTH ? 95 : 100;
        if (value < minimum || value > maximum) return false;
        *mgxUnit = unit(value); return true;
    }

    FmPatch& fm = track.fmPatch;
    if (parameter == SYNTH_PARAM_FM_ALGORITHM) {
        if (value < 0 || value > 7) return false;
        fm.algorithm = static_cast<uint8_t>(value); return true;
    }
    if (parameter == SYNTH_PARAM_FM_FEEDBACK) {
        if (value < 0 || value > 100) return false;
        fm.feedback = unit(value); return true;
    }
    if (parameter == SYNTH_PARAM_FM_INDEX) {
        if (value < 0 || value > 800) return false;
        fm.modulationIndex = static_cast<float>(value) * 0.01f; return true;
    }
    if (parameter >= SYNTH_PARAM_FM_OP1_RATIO && parameter < SYNTH_PARAM_COUNT) {
        const uint8_t offset = static_cast<uint8_t>(parameter - SYNTH_PARAM_FM_OP1_RATIO);
        const uint8_t operatorIndex = offset / 6u;
        const uint8_t field = offset % 6u;
        if (operatorIndex >= 4) return false;
        FmOperatorPatch& op = fm.operators[operatorIndex];
        if (field == 0) {
            if (value < 25 || value > 1600) return false;
            op.ratio = static_cast<float>(value) * 0.01f; return true;
        }
        if (field == 1) {
            if (value < 0 || value > 100) return false;
            op.level = unit(value); return true;
        }
        return setAdsr(op.envelope, parameter,
                       static_cast<SynthParameter>(
                           SYNTH_PARAM_FM_OP1_ATTACK + operatorIndex * 6u), value);
    }
    (void)legacy;
    return false;
}

bool synthGetParameter(const SynthTrack& track, SynthParameter parameter, int32_t& value) {
    if (parameter >= SYNTH_PARAM_COUNT) return false;
    if (parameter == SYNTH_PARAM_ENGINE) value = track.displayEngine();
    else if (parameter == SYNTH_PARAM_VOICES) value = track.voices;
    else if (parameter == SYNTH_PARAM_VOLUME) value = percent(track.volume());
    else if (parameter == SYNTH_PARAM_MG_OSC) value = track.v[0].oscMode;
    else if (parameter == SYNTH_PARAM_MG_WAVETABLE) value = track.v[0].wtIndex;
    else if (parameter == SYNTH_PARAM_MG_CUTOFF) value = percent(track.v[0].fltCutoff);
    else if (parameter == SYNTH_PARAM_MG_RESONANCE) value = percent(track.v[0].fltReso);
    else if (parameter == SYNTH_PARAM_MG_FILTER_ENV) value = percent(track.v[0].fltEnvAmt);
    else if (parameter == SYNTH_PARAM_MG_FILTER_DECAY)
        value = static_cast<int32_t>((track.v[0].filtDecRate - 0.9950f) / 0.0000495f + 0.5f);
    else if (parameter == SYNTH_PARAM_MG_AMP_DECAY)
        value = static_cast<int32_t>((track.v[0].ampDecRate - 0.9990f) / 0.0000099f + 0.5f);
    else if (parameter == SYNTH_PARAM_MGX_OSC) value = track.mgxPatch.oscMode;
    else if (parameter == SYNTH_PARAM_MGX_WAVETABLE) value = track.mgxPatch.wavetable;
    else if (parameter == SYNTH_PARAM_MGX_FILTER_MODE) value = track.mgxPatch.filterMode;
    else if (parameter == SYNTH_PARAM_MGX_LFO_DESTINATION) value = track.mgxPatch.lfoDestination;
    else if (parameter == SYNTH_PARAM_MGX_LFO_RATE)
        value = static_cast<int32_t>(track.mgxPatch.lfoRate * 100.0f + 0.5f);
    else if (parameter >= SYNTH_PARAM_MGX_AMP_ATTACK &&
             parameter <= SYNTH_PARAM_MGX_AMP_RELEASE)
        return getAdsr(track.mgxPatch.ampEnvelope, parameter,
                       SYNTH_PARAM_MGX_AMP_ATTACK, value);
    else if (parameter >= SYNTH_PARAM_MGX_FILTER_ATTACK &&
             parameter <= SYNTH_PARAM_MGX_FILTER_RELEASE)
        return getAdsr(track.mgxPatch.filterEnvelope, parameter,
                       SYNTH_PARAM_MGX_FILTER_ATTACK, value);
    else if (parameter == SYNTH_PARAM_MGX_CUTOFF) value = percent(track.mgxPatch.cutoff);
    else if (parameter == SYNTH_PARAM_MGX_RESONANCE) value = percent(track.mgxPatch.resonance);
    else if (parameter == SYNTH_PARAM_MGX_FILTER_ENV) value = percent(track.mgxPatch.filterEnvAmount);
    else if (parameter == SYNTH_PARAM_MGX_PULSE_WIDTH) value = percent(track.mgxPatch.pulseWidth);
    else if (parameter == SYNTH_PARAM_MGX_SUB_LEVEL) value = percent(track.mgxPatch.subLevel);
    else if (parameter == SYNTH_PARAM_MGX_DRIVE) value = percent(track.mgxPatch.drive);
    else if (parameter == SYNTH_PARAM_MGX_VELOCITY_AMP) value = percent(track.mgxPatch.velocityAmp);
    else if (parameter == SYNTH_PARAM_MGX_VELOCITY_FILTER) value = percent(track.mgxPatch.velocityFilter);
    else if (parameter == SYNTH_PARAM_MGX_LFO_DEPTH) value = percent(track.mgxPatch.lfoDepth);
    else if (parameter == SYNTH_PARAM_FM_ALGORITHM) value = track.fmPatch.algorithm;
    else if (parameter == SYNTH_PARAM_FM_FEEDBACK) value = percent(track.fmPatch.feedback);
    else if (parameter == SYNTH_PARAM_FM_INDEX)
        value = static_cast<int32_t>(track.fmPatch.modulationIndex * 100.0f + 0.5f);
    else if (parameter >= SYNTH_PARAM_FM_OP1_RATIO && parameter < SYNTH_PARAM_COUNT) {
        const uint8_t offset = static_cast<uint8_t>(parameter - SYNTH_PARAM_FM_OP1_RATIO);
        const uint8_t operatorIndex = offset / 6u;
        const uint8_t field = offset % 6u;
        if (operatorIndex >= 4) return false;
        const FmOperatorPatch& op = track.fmPatch.operators[operatorIndex];
        if (field == 0) value = static_cast<int32_t>(op.ratio * 100.0f + 0.5f);
        else if (field == 1) value = percent(op.level);
        else return getAdsr(op.envelope, parameter,
                            static_cast<SynthParameter>(
                                SYNTH_PARAM_FM_OP1_ATTACK + operatorIndex * 6u), value);
    } else return false;
    return true;
}

bool synthParameterFromName(const char* name, SynthParameter& parameter) {
    for (uint8_t index = 0; index < SYNTH_PARAM_COUNT; ++index)
        if (sameWord(name, kNames[index])) {
            parameter = static_cast<SynthParameter>(index);
            return true;
        }
    return false;
}

bool synthParameterRange(SynthParameter parameter, int32_t& minimum,
                         int32_t& maximum) {
    minimum = 0;
    maximum = 100;
    if (parameter >= SYNTH_PARAM_COUNT) return false;
    if (parameter == SYNTH_PARAM_ENGINE) maximum = SYNTH_ENGINE_COUNT - 1;
    else if (parameter == SYNTH_PARAM_VOICES) { minimum = 1; maximum = MAX_POLY; }
    else if (parameter == SYNTH_PARAM_MG_OSC || parameter == SYNTH_PARAM_MGX_OSC)
        maximum = OSC_COUNT - 1;
    else if (parameter == SYNTH_PARAM_MG_WAVETABLE ||
             parameter == SYNTH_PARAM_MGX_WAVETABLE) maximum = NUM_WT_TOTAL - 1;
    else if (parameter == SYNTH_PARAM_MGX_FILTER_MODE) maximum = SYNTH_FILTER_COUNT - 1;
    else if (parameter == SYNTH_PARAM_MGX_PULSE_WIDTH) { minimum = 5; maximum = 95; }
    else if (parameter == SYNTH_PARAM_MGX_LFO_DESTINATION) maximum = SYNTH_LFO_COUNT - 1;
    else if (parameter == SYNTH_PARAM_MGX_LFO_RATE) { minimum = 5; maximum = 2000; }
    else if (parameter == SYNTH_PARAM_FM_ALGORITHM) maximum = 7;
    else if (parameter == SYNTH_PARAM_FM_INDEX) maximum = 800;
    else if ((parameter >= SYNTH_PARAM_MGX_AMP_ATTACK &&
              parameter <= SYNTH_PARAM_MGX_AMP_RELEASE &&
              parameter != SYNTH_PARAM_MGX_AMP_SUSTAIN) ||
             (parameter >= SYNTH_PARAM_MGX_FILTER_ATTACK &&
              parameter <= SYNTH_PARAM_MGX_FILTER_RELEASE &&
              parameter != SYNTH_PARAM_MGX_FILTER_SUSTAIN)) maximum = 5000;
    else if (parameter >= SYNTH_PARAM_FM_OP1_RATIO) {
        const uint8_t field = static_cast<uint8_t>(
            parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u;
        if (field == 0) { minimum = 25; maximum = 1600; }
        else if (field == 2 || field == 3 || field == 5) maximum = 5000;
    }
    return true;
}

const char* synthParameterName(SynthParameter parameter) {
    return parameter < SYNTH_PARAM_COUNT ? kNames[parameter] : "unknown";
}
