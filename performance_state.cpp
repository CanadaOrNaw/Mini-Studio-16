#include "performance_state.h"
#include <string.h>

extern SynthTrack g_synths[3];
extern uint8_t g_swing;

ChordEngine g_chordEngine;
ChordSettings g_chordSettings;
HiChordPerformance g_hiChordPerformance;
MedoPerformance g_medoPerformance;
PoPatternEffects g_poPatternEffects;
PoEffectProcessor g_poEffectProcessor;
MasterEffects g_masterEffects;
Vocoder8Band g_vocoder;
uint8_t g_chordParameter = 0;
uint8_t g_chordDegree = 0;
uint8_t g_poEffectSelection = PO_FX_NONE;
bool g_poLiveEffectActive = false;
uint8_t g_medoParameter = 0;
uint8_t g_masterEffectSelection = 0;
uint8_t g_masterEffectParameter = 0;
uint8_t g_vocoderParameter = 0;
bool g_hiChordBounceActive = false;
int8_t g_hiChordBounceTrack = -1;
uint8_t g_hiChordDrumKit = 0;
uint8_t g_hiChordGrooveStyle = 0;
uint8_t g_hiChordGrooveVariation = 0;
uint8_t g_hiChordPracticeSong = 0;
uint8_t g_hiChordPracticePosition = 0;
uint8_t g_hiChordEarLevel = 0;
uint8_t g_hiChordEarTarget = 0;
uint16_t g_hiChordEarScore = 0;
uint8_t g_hiChordArpPattern = ARP_UP;
uint8_t g_hiChordArpLayer = ARP_CHORD_ONLY;
uint8_t g_hiChordArpRate = 1;
uint8_t g_hiChordRepeatRate = 1;
PerformancePreset g_performancePresets[4];

void performanceStateInit() {
    g_chordEngine.reset();
    g_chordSettings = ChordEngine::defaults();
    g_hiChordPerformance.reset();
    g_medoPerformance.reset();
    g_poPatternEffects.clear();
    g_poEffectProcessor.reset();
    g_masterEffects.reset();
    g_vocoder.reset();
    g_chordParameter = 0;
    g_chordDegree = 0;
    g_poEffectSelection = PO_FX_NONE;
    g_poLiveEffectActive = false;
    g_medoParameter = 0;
    g_masterEffectSelection = 0;
    g_masterEffectParameter = 0;
    g_vocoderParameter = 0;
    g_swing = 50;
    g_hiChordBounceActive = false;
    g_hiChordBounceTrack = -1;
    g_hiChordDrumKit = g_hiChordGrooveStyle = g_hiChordGrooveVariation = 0;
    g_hiChordPracticeSong = g_hiChordPracticePosition = 0;
    g_hiChordEarLevel = g_hiChordEarTarget = 0;
    g_hiChordEarScore = 0;
    g_hiChordArpPattern = ARP_UP;
    g_hiChordArpLayer = ARP_CHORD_ONLY;
    g_hiChordArpRate = 1;
    g_hiChordRepeatRate = 1;
    memset(g_performancePresets, 0, sizeof(g_performancePresets));
}

bool performanceStorePreset(uint8_t index) {
    if (index >= 4) return false;
    PerformancePreset &preset = g_performancePresets[index];
    preset.valid = 1; preset.chord = g_chordSettings;
    for (uint8_t synth = 0; synth < 3; ++synth)
        synthProjectEncode(g_synths[synth], preset.synths[synth]);
    return true;
}

bool performanceRecallPreset(uint8_t index) {
    if (index >= 4 || !g_performancePresets[index].valid) return false;
    const PerformancePreset &preset = g_performancePresets[index];
    g_chordSettings = preset.chord; g_chordEngine.reset();
    for (uint8_t synth = 0; synth < 3; ++synth)
        if (!synthProjectDecode(preset.synths[synth], g_synths[synth])) return false;
    return true;
}
bool performancePresetValid(uint8_t index) {
    return index < 4 && g_performancePresets[index].valid != 0;
}
