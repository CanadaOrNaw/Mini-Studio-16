#include "synth_ui_model.h"

namespace {
const SynthUiRow kLegacy[] = {
    {"OSC", SYNTH_PARAM_MG_OSC}, {"WTABLE", SYNTH_PARAM_MG_WAVETABLE},
    {"CUTOFF", SYNTH_PARAM_MG_CUTOFF}, {"RESO", SYNTH_PARAM_MG_RESONANCE},
    {"ENV AMT", SYNTH_PARAM_MG_FILTER_ENV}, {"FLT DEC", SYNTH_PARAM_MG_FILTER_DECAY},
    {"AMP DEC", SYNTH_PARAM_MG_AMP_DECAY}, {"VOLUME", SYNTH_PARAM_VOLUME},
    {"VOICES", SYNTH_PARAM_VOICES},
};
const SynthUiRow kCommon[] = {
    {"ENGINE", SYNTH_PARAM_ENGINE}, {"VOICES", SYNTH_PARAM_VOICES},
    {"VOLUME", SYNTH_PARAM_VOLUME},
};
const SynthUiRow kMgxOsc[] = {
    {"OSC", SYNTH_PARAM_MGX_OSC}, {"WTABLE", SYNTH_PARAM_MGX_WAVETABLE},
    {"PULSE", SYNTH_PARAM_MGX_PULSE_WIDTH}, {"SUB", SYNTH_PARAM_MGX_SUB_LEVEL},
    {"DRIVE", SYNTH_PARAM_MGX_DRIVE}, {"VOLUME", SYNTH_PARAM_VOLUME},
    {"VOICES", SYNTH_PARAM_VOICES},
};
const SynthUiRow kMgxFilter[] = {
    {"MODE", SYNTH_PARAM_MGX_FILTER_MODE}, {"CUTOFF", SYNTH_PARAM_MGX_CUTOFF},
    {"RESO", SYNTH_PARAM_MGX_RESONANCE}, {"ENV AMT", SYNTH_PARAM_MGX_FILTER_ENV},
    {"VEL FLT", SYNTH_PARAM_MGX_VELOCITY_FILTER},
};
const SynthUiRow kMgxAmp[] = {
    {"ATTACK", SYNTH_PARAM_MGX_AMP_ATTACK}, {"DECAY", SYNTH_PARAM_MGX_AMP_DECAY},
    {"SUSTAIN", SYNTH_PARAM_MGX_AMP_SUSTAIN}, {"RELEASE", SYNTH_PARAM_MGX_AMP_RELEASE},
    {"VEL AMP", SYNTH_PARAM_MGX_VELOCITY_AMP},
};
const SynthUiRow kMgxFilterEnvelope[] = {
    {"ATTACK", SYNTH_PARAM_MGX_FILTER_ATTACK}, {"DECAY", SYNTH_PARAM_MGX_FILTER_DECAY},
    {"SUSTAIN", SYNTH_PARAM_MGX_FILTER_SUSTAIN}, {"RELEASE", SYNTH_PARAM_MGX_FILTER_RELEASE},
};
const SynthUiRow kMgxLfo[] = {
    {"DEST", SYNTH_PARAM_MGX_LFO_DESTINATION}, {"RATE", SYNTH_PARAM_MGX_LFO_RATE},
    {"DEPTH", SYNTH_PARAM_MGX_LFO_DEPTH},
};
const SynthUiRow kFmGlobal[] = {
    {"ALGO", SYNTH_PARAM_FM_ALGORITHM}, {"FEEDBACK", SYNTH_PARAM_FM_FEEDBACK},
    {"INDEX", SYNTH_PARAM_FM_INDEX}, {"VOLUME", SYNTH_PARAM_VOLUME},
    {"VOICES", SYNTH_PARAM_VOICES},
};
const SynthUiRow kFmOperators[4][6] = {
    {{"RATIO", SYNTH_PARAM_FM_OP1_RATIO}, {"LEVEL", SYNTH_PARAM_FM_OP1_LEVEL},
     {"ATTACK", SYNTH_PARAM_FM_OP1_ATTACK}, {"DECAY", SYNTH_PARAM_FM_OP1_DECAY},
     {"SUSTAIN", SYNTH_PARAM_FM_OP1_SUSTAIN}, {"RELEASE", SYNTH_PARAM_FM_OP1_RELEASE}},
    {{"RATIO", SYNTH_PARAM_FM_OP2_RATIO}, {"LEVEL", SYNTH_PARAM_FM_OP2_LEVEL},
     {"ATTACK", SYNTH_PARAM_FM_OP2_ATTACK}, {"DECAY", SYNTH_PARAM_FM_OP2_DECAY},
     {"SUSTAIN", SYNTH_PARAM_FM_OP2_SUSTAIN}, {"RELEASE", SYNTH_PARAM_FM_OP2_RELEASE}},
    {{"RATIO", SYNTH_PARAM_FM_OP3_RATIO}, {"LEVEL", SYNTH_PARAM_FM_OP3_LEVEL},
     {"ATTACK", SYNTH_PARAM_FM_OP3_ATTACK}, {"DECAY", SYNTH_PARAM_FM_OP3_DECAY},
     {"SUSTAIN", SYNTH_PARAM_FM_OP3_SUSTAIN}, {"RELEASE", SYNTH_PARAM_FM_OP3_RELEASE}},
    {{"RATIO", SYNTH_PARAM_FM_OP4_RATIO}, {"LEVEL", SYNTH_PARAM_FM_OP4_LEVEL},
     {"ATTACK", SYNTH_PARAM_FM_OP4_ATTACK}, {"DECAY", SYNTH_PARAM_FM_OP4_DECAY},
     {"SUSTAIN", SYNTH_PARAM_FM_OP4_SUSTAIN}, {"RELEASE", SYNTH_PARAM_FM_OP4_RELEASE}},
};

template <size_t N> uint8_t count(const SynthUiRow (&)[N]) {
    return static_cast<uint8_t>(N);
}
}  // namespace

SynthSoundBank synthFirstSoundBank(SynthEngine engine) {
    if (engine == SYNTH_ENGINE_MG) return SYNTH_BANK_LEGACY;
    if (engine == SYNTH_ENGINE_MGX) return SYNTH_BANK_MGX_OSC;
    return SYNTH_BANK_FM_GLOBAL;
}

SynthSoundBank synthNextSoundBank(SynthEngine engine, SynthSoundBank current) {
    static const SynthSoundBank mg[] = {SYNTH_BANK_LEGACY, SYNTH_BANK_COMMON};
    static const SynthSoundBank mgx[] = {SYNTH_BANK_MGX_OSC, SYNTH_BANK_MGX_FILTER,
        SYNTH_BANK_MGX_AMP, SYNTH_BANK_MGX_FILTER_ENV, SYNTH_BANK_MGX_LFO,
        SYNTH_BANK_COMMON};
    static const SynthSoundBank fm[] = {SYNTH_BANK_FM_GLOBAL, SYNTH_BANK_FM_OP1,
        SYNTH_BANK_FM_OP2, SYNTH_BANK_FM_OP3, SYNTH_BANK_FM_OP4, SYNTH_BANK_COMMON};
    const SynthSoundBank* banks = engine == SYNTH_ENGINE_MG ? mg :
                                  engine == SYNTH_ENGINE_MGX ? mgx : fm;
    const uint8_t length = engine == SYNTH_ENGINE_MG ? 2 : 6;
    for (uint8_t index = 0; index < length; ++index)
        if (banks[index] == current) return banks[(index + 1u) % length];
    return banks[0];
}

const char* synthSoundBankName(SynthSoundBank bank) {
    static const char* const names[SYNTH_BANK_COUNT] = {
        "LEGACY", "COMMON", "MGX OSC", "MGX FILTER", "MGX AMP",
        "MGX FENV", "MGX LFO", "FM GLOBAL", "FM OP1", "FM OP2",
        "FM OP3", "FM OP4",
    };
    return bank < SYNTH_BANK_COUNT ? names[bank] : "?";
}

uint8_t synthSoundBankRows(SynthSoundBank bank) {
    switch (bank) {
        case SYNTH_BANK_LEGACY: return count(kLegacy);
        case SYNTH_BANK_COMMON: return count(kCommon);
        case SYNTH_BANK_MGX_OSC: return count(kMgxOsc);
        case SYNTH_BANK_MGX_FILTER: return count(kMgxFilter);
        case SYNTH_BANK_MGX_AMP: return count(kMgxAmp);
        case SYNTH_BANK_MGX_FILTER_ENV: return count(kMgxFilterEnvelope);
        case SYNTH_BANK_MGX_LFO: return count(kMgxLfo);
        case SYNTH_BANK_FM_GLOBAL: return count(kFmGlobal);
        case SYNTH_BANK_FM_OP1:
        case SYNTH_BANK_FM_OP2:
        case SYNTH_BANK_FM_OP3:
        case SYNTH_BANK_FM_OP4: return 6;
        default: return 0;
    }
}

SynthUiRow synthSoundBankRow(SynthSoundBank bank, uint8_t row) {
    static const SynthUiRow invalid = {"?", SYNTH_PARAM_COUNT};
    if (row >= synthSoundBankRows(bank)) return invalid;
    switch (bank) {
        case SYNTH_BANK_LEGACY: return kLegacy[row];
        case SYNTH_BANK_COMMON: return kCommon[row];
        case SYNTH_BANK_MGX_OSC: return kMgxOsc[row];
        case SYNTH_BANK_MGX_FILTER: return kMgxFilter[row];
        case SYNTH_BANK_MGX_AMP: return kMgxAmp[row];
        case SYNTH_BANK_MGX_FILTER_ENV: return kMgxFilterEnvelope[row];
        case SYNTH_BANK_MGX_LFO: return kMgxLfo[row];
        case SYNTH_BANK_FM_GLOBAL: return kFmGlobal[row];
        case SYNTH_BANK_FM_OP1:
        case SYNTH_BANK_FM_OP2:
        case SYNTH_BANK_FM_OP3:
        case SYNTH_BANK_FM_OP4:
            return kFmOperators[bank - SYNTH_BANK_FM_OP1][row];
        default: return invalid;
    }
}

int32_t synthSoundParameterStep(SynthParameter parameter, bool fine) {
    if (parameter == SYNTH_PARAM_ENGINE || parameter == SYNTH_PARAM_VOICES ||
        parameter == SYNTH_PARAM_MG_OSC || parameter == SYNTH_PARAM_MG_WAVETABLE ||
        parameter == SYNTH_PARAM_MGX_OSC || parameter == SYNTH_PARAM_MGX_WAVETABLE ||
        parameter == SYNTH_PARAM_MGX_FILTER_MODE ||
        parameter == SYNTH_PARAM_MGX_LFO_DESTINATION ||
        parameter == SYNTH_PARAM_FM_ALGORITHM) return 1;
    if (parameter == SYNTH_PARAM_MGX_LFO_RATE) return fine ? 5 : 25;
    if (parameter == SYNTH_PARAM_FM_INDEX) return fine ? 5 : 25;
    if (parameter >= SYNTH_PARAM_FM_OP1_RATIO && parameter < SYNTH_PARAM_COUNT &&
        ((parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u) == 0)
        return fine ? 1 : 25;
    if ((parameter >= SYNTH_PARAM_MGX_AMP_ATTACK &&
         parameter <= SYNTH_PARAM_MGX_AMP_RELEASE &&
         parameter != SYNTH_PARAM_MGX_AMP_SUSTAIN) ||
        (parameter >= SYNTH_PARAM_MGX_FILTER_ATTACK &&
         parameter <= SYNTH_PARAM_MGX_FILTER_RELEASE &&
         parameter != SYNTH_PARAM_MGX_FILTER_SUSTAIN) ||
        (parameter >= SYNTH_PARAM_FM_OP1_RATIO && parameter < SYNTH_PARAM_COUNT &&
         (((parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u) == 2u ||
          ((parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u) == 3u ||
          ((parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u) == 5u)))
        return fine ? 5 : 25;
    return fine ? 1 : 5;
}
