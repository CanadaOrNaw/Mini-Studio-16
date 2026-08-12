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
    g_hiChordArpPattern = ARP_RANDOM; g_hiChordArpLayer = ARP_CHORD_AND_BASS;
    g_hiChordArpRate = 4; g_hiChordRepeatRate = 8;
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
    assert(g_hiChordArpPattern == ARP_RANDOM && g_hiChordArpLayer == ARP_CHORD_AND_BASS);
    assert(g_hiChordArpRate == 4 && g_hiChordRepeatRate == 8);
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
    assert(g_swing == 66);
    assert(performancePresetValid(0));
    SavePerformanceState corrupt = saved;
    corrupt.chord.key = 12; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.poEffects[1][2] = 255; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.medoTracks[0].octave = 99; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.hiChordArpRate = 3; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.medoScale = 99; assert(!performanceProjectValidate(corrupt));
    corrupt = saved; corrupt.presets[0].synths[0].engine = 99; assert(!performanceProjectValidate(corrupt));
    return 0;
}
