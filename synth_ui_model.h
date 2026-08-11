#pragma once

#include "synth_parameters.h"

#include <stdint.h>

enum SynthSoundBank : uint8_t {
    SYNTH_BANK_LEGACY = 0,
    SYNTH_BANK_COMMON,
    SYNTH_BANK_MGX_OSC,
    SYNTH_BANK_MGX_FILTER,
    SYNTH_BANK_MGX_AMP,
    SYNTH_BANK_MGX_FILTER_ENV,
    SYNTH_BANK_MGX_LFO,
    SYNTH_BANK_FM_GLOBAL,
    SYNTH_BANK_FM_OP1,
    SYNTH_BANK_FM_OP2,
    SYNTH_BANK_FM_OP3,
    SYNTH_BANK_FM_OP4,
    SYNTH_BANK_COUNT,
};

struct SynthUiRow {
    const char* label;
    SynthParameter parameter;
};

SynthSoundBank synthFirstSoundBank(SynthEngine engine);
SynthSoundBank synthNextSoundBank(SynthEngine engine, SynthSoundBank current);
bool synthSoundBankBelongs(SynthEngine engine, SynthSoundBank bank);
SynthSoundBank synthEnsureSoundBank(SynthEngine engine, SynthSoundBank bank);
const char* synthSoundBankName(SynthSoundBank bank);
uint8_t synthSoundBankRows(SynthSoundBank bank);
SynthUiRow synthSoundBankRow(SynthSoundBank bank, uint8_t row);
int32_t synthSoundParameterStep(SynthParameter parameter, bool fine);

