#include "../synth_parameters.h"
#include "../synth_project.h"

#include <assert.h>
#include <math.h>
#include <string.h>

int main() {
    SynthTrack source = {};
    source.init();
    source.setEngine(SYNTH_ENGINE_FM4);
    source.setVoices(3);
    assert(synthSetParameter(source, SYNTH_PARAM_MGX_FILTER_MODE, SYNTH_FILTER_HP));
    assert(synthSetParameter(source, SYNTH_PARAM_MGX_PULSE_WIDTH, 37));
    assert(synthSetParameter(source, SYNTH_PARAM_MGX_AMP_ATTACK, 1234));
    assert(synthSetParameter(source, SYNTH_PARAM_FM_ALGORITHM, 6));
    assert(synthSetParameter(source, SYNTH_PARAM_FM_INDEX, 725));
    assert(synthSetParameter(source, SYNTH_PARAM_FM_OP3_RATIO, 675));
    assert(synthSetParameter(source, SYNTH_PARAM_FM_OP4_RELEASE, 4321));

    SaveSynthEngineState saved = {};
    synthProjectEncode(source, saved);
    assert(synthProjectValidate(saved));

    SynthTrack loaded = {};
    loaded.init();
    assert(synthProjectDecode(saved, loaded));
    // P2-8: decode requests the engine; the audio task applies it at the
    // next block boundary. displayEngine() reflects it immediately, and
    // applyPendingEngine() simulates that audio-task step here.
    assert(loaded.displayEngine() == SYNTH_ENGINE_FM4);
    loaded.applyPendingEngine();
    assert(loaded.engine == SYNTH_ENGINE_FM4);
    assert(loaded.mgxPatch.filterMode == SYNTH_FILTER_HP);
    assert(fabsf(loaded.mgxPatch.pulseWidth - 0.37f) < 0.0001f);
    assert(loaded.mgxPatch.ampEnvelope.attackMs == 1234);
    assert(loaded.fmPatch.algorithm == 6);
    assert(fabsf(loaded.fmPatch.modulationIndex - 7.25f) < 0.0001f);
    assert(fabsf(loaded.fmPatch.operators[2].ratio - 6.75f) < 0.0001f);
    assert(loaded.fmPatch.operators[3].envelope.releaseMs == 4321);

    SaveSynthEngineState reencoded = {};
    synthProjectEncode(loaded, reencoded);
    assert(memcmp(&saved, &reencoded, sizeof(saved)) == 0);

    // Saving before the audio task reaches its next block boundary must
    // still encode the engine the user just selected.
    loaded.requestEngine(SYNTH_ENGINE_MGX);
    SaveSynthEngineState pendingSaved = {};
    synthProjectEncode(loaded, pendingSaved);
    assert(pendingSaved.engine == SYNTH_ENGINE_MGX);
    assert(loaded.engine == SYNTH_ENGINE_FM4);
    loaded.applyPendingEngine();
    assert(loaded.engine == SYNTH_ENGINE_MGX);

    SaveSynthEngineState malformed = saved;
    malformed.engine = SYNTH_ENGINE_COUNT;
    assert(!synthProjectValidate(malformed));
    malformed = saved;
    malformed.fm.operators[1].ratioCent = 24;
    assert(!synthProjectValidate(malformed));
    malformed = saved;
    malformed.mgx.ampEnvelope.sustainQ15 = 32768;
    assert(!synthProjectValidate(malformed));
    malformed = saved;
    malformed.fm.modulationIndexCent = 801;
    assert(!synthProjectValidate(malformed));

    SynthTrack legacy = {};
    legacy.init();
    legacy.setEngine(SYNTH_ENGINE_FM4);
    synthProjectMigrateLegacy(legacy);
    legacy.applyPendingEngine();
    assert(legacy.engine == SYNTH_ENGINE_MG);
    return 0;
}
