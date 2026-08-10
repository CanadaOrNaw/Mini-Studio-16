#include "../synth_ui_model.h"

#include <assert.h>
#include <string.h>

int main() {
    assert(synthFirstSoundBank(SYNTH_ENGINE_MG) == SYNTH_BANK_LEGACY);
    assert(synthFirstSoundBank(SYNTH_ENGINE_MGX) == SYNTH_BANK_MGX_OSC);
    assert(synthFirstSoundBank(SYNTH_ENGINE_FM4) == SYNTH_BANK_FM_GLOBAL);

    SynthSoundBank bank = SYNTH_BANK_MGX_OSC;
    for (uint8_t index = 0; index < 6; ++index)
        bank = synthNextSoundBank(SYNTH_ENGINE_MGX, bank);
    assert(bank == SYNTH_BANK_MGX_OSC);
    bank = SYNTH_BANK_FM_GLOBAL;
    for (uint8_t index = 0; index < 6; ++index)
        bank = synthNextSoundBank(SYNTH_ENGINE_FM4, bank);
    assert(bank == SYNTH_BANK_FM_GLOBAL);

    assert(synthSoundBankRows(SYNTH_BANK_LEGACY) == 9);
    assert(synthSoundBankRows(SYNTH_BANK_FM_OP1) == 6);
    const SynthUiRow firstOperator = synthSoundBankRow(SYNTH_BANK_FM_OP1, 0);
    const SynthUiRow fourthOperator = synthSoundBankRow(SYNTH_BANK_FM_OP4, 5);
    assert(strcmp(firstOperator.label, "RATIO") == 0);
    assert(firstOperator.parameter == SYNTH_PARAM_FM_OP1_RATIO);
    assert(fourthOperator.parameter == SYNTH_PARAM_FM_OP4_RELEASE);
    assert(synthSoundBankRow(SYNTH_BANK_FM_OP4, 6).parameter == SYNTH_PARAM_COUNT);
    assert(synthSoundParameterStep(SYNTH_PARAM_FM_OP1_RATIO, false) == 25);
    assert(synthSoundParameterStep(SYNTH_PARAM_FM_OP1_RATIO, true) == 1);
    assert(synthSoundParameterStep(SYNTH_PARAM_MGX_AMP_ATTACK, false) == 25);
    assert(synthSoundParameterStep(SYNTH_PARAM_MGX_AMP_ATTACK, true) == 5);
    return 0;
}
