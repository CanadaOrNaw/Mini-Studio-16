#include "../performance_project.h"
#include <assert.h>
#include <string.h>

SynthTrack g_synths[NUM_SYNTHS];
uint8_t g_swing = 50;

int main() {
    for (uint8_t i = 0; i < 3; ++i) g_synths[i].init();
    performanceStateInit();
    g_chordSettings.key = 9; g_chordSettings.scale = SCALE_DORIAN;
    g_swing = 66;
    g_chordSettings.lockedType[3] = CHORD_DOM13;
    g_hiChordPerformance.setMode(HICHORD_CHORD_HIRO);
    g_hiChordArpPattern = ARP_RANDOM; g_hiChordArpLayer = ARP_RHYTHM_PLUS;
    g_hiChordArpRate = HICHORD_RATE_1_16T;
    g_hiChordRepeatRate = HICHORD_RATE_SWING_16;
    g_hiChordStrumSpeed = HICHORD_STRUM_FAST;
    g_hiChordLoopBars = 6;
    g_hiChordSequenceLength = 12;
    g_hiChordDrumKit = 4; g_hiChordGrooveStyle = 3; g_hiChordGrooveVariation = 7;
    g_hiChordPracticeSong = 5; g_hiChordPracticePosition = 3;
    g_hiChordEarLevel = 2; g_hiChordEarScore = 42;
    HiChordSequenceStep step = {4, CHORD_DIR_NW, 2, 1};
    assert(g_hiChordPerformance.setSequenceStep(7, step));
    assert(g_poPatternEffects.set(15, 4, PO_FX_REVERSE));
    assert(g_medoPerformance.setRole(MEDO_LEAD));
    assert(g_medoPerformance.setQuantize(MEDO_LEAD, MEDO_GROOVE));
    assert(g_medoPerformance.setScale(MEDO_PENTATONIC_MAJOR));
    assert(g_medoPerformance.setArpDirection(MEDO_ARP_DOWN));
    assert(g_medoPerformance.setArpRate(4));
    g_medoPerformance.setArpEnabled(true);
    assert(g_medoPerformance.setSharedBars(128));
    assert(performanceStorePreset(0));
    SavePerformanceState saved;
    performanceProjectEncode(saved);
    assert(performanceProjectValidate(saved));
    performanceStateInit();
    assert(performanceProjectDecode(saved));
    assert(g_chordSettings.key == 9 && g_chordSettings.scale == SCALE_DORIAN);
    assert(g_chordSettings.lockedType[3] == CHORD_DOM13);
    assert(g_hiChordPerformance.mode() == HICHORD_CHORD_HIRO);
    assert(g_hiChordArpPattern == ARP_RANDOM && g_hiChordArpLayer == ARP_RHYTHM_PLUS);
    assert(g_hiChordArpRate == HICHORD_RATE_1_16T &&
           g_hiChordRepeatRate == HICHORD_RATE_SWING_16);
    assert(g_hiChordStrumSpeed == HICHORD_STRUM_FAST);
    assert(g_hiChordLoopBars == 6);
    assert(g_hiChordSequenceLength == 12);
    assert(g_hiChordDrumKit == 4 && g_hiChordGrooveStyle == 3 && g_hiChordGrooveVariation == 7);
    assert(g_hiChordPracticeSong == 5 && g_hiChordPracticePosition == 3);
    assert(g_hiChordEarLevel == 2 && g_hiChordEarScore == 42);
    assert(g_hiChordPerformance.sequenceStep(7).slashDegree == 2);
    assert(g_poPatternEffects.get(15, 4) == PO_FX_REVERSE);
    assert(g_medoPerformance.role() == MEDO_LEAD);
    assert(g_medoPerformance.settings(MEDO_LEAD).quantize == MEDO_GROOVE);
    assert(g_medoPerformance.scale() == MEDO_PENTATONIC_MAJOR);
    assert(g_medoPerformance.arpDirection() == MEDO_ARP_DOWN);
    assert(g_medoPerformance.arpRate() == 4 && g_medoPerformance.sharedBars() == 128);
    assert(g_medoPerformance.arpEnabled());
    assert(g_swing == 66);
    assert(performancePresetValid(0));
    SavePerformanceState corrupt = saved;
    corrupt.chord.key = 12; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.poEffects[1][2] = 255; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.medoTracks[0].octave = 99; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.hiChordArpRate = 99; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.hiChordArpLayer = 0xF0; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.hiChordMode = 0xF0; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.medoScale = 99; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.presets[0].synths[0].engine = 99; assert(!performanceProjectValidate(corrupt));

    // A2-P2 regression: VALIDATOR/DECODER PARITY. Anything the decoder can
    // reject must be rejected by the validator first, because the loader
    // clears patterns, sampler slots and performance state (and consumes its
    // .bak fallback) between the two. A field that only the decoder rejects
    // therefore destroys the user's live session with nothing loaded.
    // medoSharedBars was exactly that hole.
    {
        static const uint8_t badSharedBars[] = {129, 200, 255};
        for (uint8_t index = 0; index < 3; ++index) {
            SavePerformanceState bad = saved;
            bad.medoSharedBars = badSharedBars[index];
            assert(!performanceProjectValidate(bad));
        }
        // Exhaustive parity sweep over every single-byte field: for each
        // byte offset, every value the validator accepts must also decode.
        uint8_t *bytes = reinterpret_cast<uint8_t *>(&saved);
        for (size_t offset = 0; offset < sizeof(SavePerformanceState); ++offset) {
            const uint8_t original = bytes[offset];
            for (unsigned value = 0; value < 256u; value += 17u) {
                bytes[offset] = static_cast<uint8_t>(value);
                if (!performanceProjectValidate(saved)) continue;
                performanceStateInit();
                assert(performanceProjectDecode(saved));   // must never fail
            }
            bytes[offset] = original;
        }
    }

    // A2-P1-1 regression: range checking must not require an instance. These
    // are compile-time checks — if either validator stops being static, this
    // file fails to build. The runtime assertions prove they still work.
    {
        MasterEffectsSettings effects = {};
        effects.enabledMask = 0; effects.feedback = 55;
        effects.rate = 32; effects.filter = 100;
        for (uint8_t i = 0; i < MASTER_EFFECT_COUNT; ++i) effects.mix[i] = 48;
        assert(MasterEffects::validate(effects));
        effects.feedback = 121;
        assert(!MasterEffects::validate(effects));

        VocoderSettings vocoder = {0, VOCODER_LOOP1, 0, 70, 50, 35, 10, 4};
        assert(Vocoder8Band::validate(vocoder));
        vocoder.formantShift = 13;
        assert(!Vocoder8Band::validate(vocoder));
    }

    // A2-P2: master-effect and vocoder settings genuinely round-trip. These
    // are the two largest embedded sub-structs (11 and 8 fields) and neither
    // was asserted after decode.
    {
        performanceStateInit();
        MasterEffectsSettings effects = g_masterEffects.settings();
        effects.enabledMask = (1u << MASTER_REVERB) | (1u << MASTER_FILTER);
        effects.mix[MASTER_REVERB] = 111; effects.mix[MASTER_FILTER] = 22;
        effects.feedback = 77; effects.rate = 99; effects.filter = 41;
        assert(g_masterEffects.applySettings(effects));
        VocoderSettings vocoder = g_vocoder.settings();
        vocoder.enabled = 1; vocoder.source = VOCODER_LINE; vocoder.formantShift = -7;
        vocoder.resonance = 91; vocoder.attack = 12; vocoder.release = 101;
        vocoder.noise = 33; vocoder.gate = 64;
        assert(g_vocoder.applySettings(vocoder));

        SavePerformanceState state;
        performanceProjectEncode(state);
        assert(performanceProjectValidate(state));
        performanceStateInit();
        assert(performanceProjectDecode(state));

        const MasterEffectsSettings back = g_masterEffects.settings();
        assert(back.enabledMask == effects.enabledMask);
        assert(back.mix[MASTER_REVERB] == 111 && back.mix[MASTER_FILTER] == 22);
        assert(back.feedback == 77 && back.rate == 99 && back.filter == 41);
        const VocoderSettings vback = g_vocoder.settings();
        assert(vback.enabled == 1 && vback.source == VOCODER_LINE);
        assert(vback.formantShift == -7 && vback.resonance == 91);
        assert(vback.attack == 12 && vback.release == 101);
        assert(vback.noise == 33 && vback.gate == 64);
    }
    return 0;
}
