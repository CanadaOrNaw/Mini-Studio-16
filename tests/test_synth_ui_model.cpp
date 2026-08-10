#include "../synth_ui_model.h"

#include <assert.h>

int main() {
    assert(synthFirstSoundBank(SYNTH_ENGINE_MG) == SYNTH_BANK_LEGACY);
    assert(synthFirstSoundBank(SYNTH_ENGINE_MGX) == SYNTH_BANK_MGX_OSC);
    assert(synthFirstSoundBank(SYNTH_ENGINE_FM4) == SYNTH_BANK_FM_GLOBAL);

    for (uint8_t engine = 0; engine < SYNTH_ENGINE_COUNT; ++engine) {
        const SynthEngine selected = static_cast<SynthEngine>(engine);
        SynthSoundBank bank = synthFirstSoundBank(selected);
        const uint8_t expected = selected == SYNTH_ENGINE_MG ? 2 : 6;
        for (uint8_t page = 0; page < expected; ++page) {
            const uint8_t rows = synthSoundBankRows(bank);
            assert(rows > 0 && rows <= 9);
            for (uint8_t row = 0; row < rows; ++row)
                assert(synthSoundBankRow(bank, row).parameter < SYNTH_PARAM_COUNT);
            bank = synthNextSoundBank(selected, bank);
        }
        assert(bank == synthFirstSoundBank(selected));
    }

    assert(synthSoundBankRow(SYNTH_BANK_FM_OP4, 0).parameter ==
           SYNTH_PARAM_FM_OP4_RATIO);
    assert(synthSoundBankRow(SYNTH_BANK_FM_OP4, 5).parameter ==
           SYNTH_PARAM_FM_OP4_RELEASE);
    assert(synthSoundParameterStep(SYNTH_PARAM_FM_OP1_RATIO, true) == 1);
    assert(synthSoundParameterStep(SYNTH_PARAM_MGX_AMP_ATTACK, false) == 25);
    return 0;
}
