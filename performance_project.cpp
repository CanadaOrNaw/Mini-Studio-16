#include "performance_project.h"
#include <string.h>

extern uint8_t g_swing;

static SaveChordSettings encodeChord(const ChordSettings &in) {
    SaveChordSettings out = {};
    out.key = in.key; out.scale = in.scale; out.map = in.map; out.octave = in.octave;
    out.bassMode = in.bassMode; out.voiceLeading = in.voiceLeading ? 1 : 0;
    memcpy(out.octaveShift, in.octaveShift, 7); memcpy(out.inversion, in.inversion, 7);
    memcpy(out.lockedType, in.lockedType, 7); return out;
}
static bool validChord(const SaveChordSettings &in) {
    if (in.key > 11 || in.scale >= CHORD_SCALE_COUNT || in.map > CHORD_MAP_CHROMATIC ||
        in.octave < 2 || in.octave > 6 || in.bassMode > CHORD_BASS_SLASH || in.voiceLeading > 1)
        return false;
    for (uint8_t i = 0; i < 7; ++i)
        if (in.octaveShift[i] < -2 || in.octaveShift[i] > 2 ||
            in.inversion[i] < -2 || in.inversion[i] > 3 ||
            in.lockedType[i] > CHORD_TYPE_COUNT) return false;
    return true;
}
static ChordSettings decodeChord(const SaveChordSettings &in) {
    ChordSettings out;
    out.key = in.key; out.scale = static_cast<ChordScale>(in.scale);
    out.map = static_cast<ChordMap>(in.map); out.octave = in.octave;
    out.bassMode = static_cast<ChordBassMode>(in.bassMode); out.voiceLeading = in.voiceLeading != 0;
    memcpy(out.octaveShift, in.octaveShift, 7); memcpy(out.inversion, in.inversion, 7);
    memcpy(out.lockedType, in.lockedType, 7); return out;
}

void performanceProjectEncode(SavePerformanceState &out) {
    memset(&out, 0, sizeof(out)); out.chord = encodeChord(g_chordSettings);
    out.hiChordMode = g_hiChordPerformance.mode();
    for (uint8_t i = 0; i < 16; ++i) out.chordSequence[i] = g_hiChordPerformance.sequenceStep(i);
    for (uint8_t p = 0; p < 16; ++p) for (uint8_t s = 0; s < 16; ++s)
        out.poEffects[p][s] = g_poPatternEffects.get(p, s);
    out.medoRole = g_medoPerformance.role();
    for (uint8_t role = 0; role < MEDO_ROLE_COUNT; ++role) {
        const MedoTrackSettings &settings = g_medoPerformance.settings(static_cast<MedoRole>(role));
        out.medoTracks[role] = {settings.volume, settings.octave, static_cast<uint8_t>(settings.quantize)};
    }
    out.masterEffects = g_masterEffects.settings();
    out.vocoder = g_vocoder.settings();
    out.swing = g_swing;
    out.hiChordArpPattern = g_hiChordArpPattern;
    out.hiChordArpLayer = g_hiChordArpLayer;
    out.hiChordArpRate = g_hiChordArpRate;
    out.hiChordRepeatRate = g_hiChordRepeatRate;
    out.hiChordDrumKit = g_hiChordDrumKit;
    out.hiChordGrooveStyle = g_hiChordGrooveStyle;
    out.hiChordGrooveVariation = g_hiChordGrooveVariation;
    out.hiChordPracticeSong = g_hiChordPracticeSong;
    out.hiChordPracticePosition = g_hiChordPracticePosition;
    out.hiChordEarLevel = g_hiChordEarLevel;
    out.hiChordEarScore = g_hiChordEarScore;
    out.medoScale = g_medoPerformance.scale();
    out.medoArpDirection = g_medoPerformance.arpDirection();
    out.medoArpRate = g_medoPerformance.arpRate();
    out.medoSharedBars = static_cast<uint8_t>(g_medoPerformance.sharedBars() == 128 ? 0 : g_medoPerformance.sharedBars());
    for (uint8_t preset = 0; preset < 4; ++preset) {
        out.presets[preset].valid = g_performancePresets[preset].valid;
        out.presets[preset].chord = encodeChord(g_performancePresets[preset].chord);
        memcpy(out.presets[preset].synths, g_performancePresets[preset].synths,
               sizeof(out.presets[preset].synths));
    }
}

bool performanceProjectValidate(const SavePerformanceState &in) {
    if (!validChord(in.chord) || in.hiChordMode >= HICHORD_MODE_COUNT ||
        in.medoRole >= MEDO_ROLE_COUNT || in.swing < 50 || in.swing > 75 ||
        in.hiChordArpPattern >= ARP_PATTERN_COUNT || in.hiChordArpLayer > ARP_CHORD_AND_BASS ||
        (in.hiChordArpRate != 1 && in.hiChordArpRate != 2 && in.hiChordArpRate != 4 && in.hiChordArpRate != 8) ||
        (in.hiChordRepeatRate != 1 && in.hiChordRepeatRate != 2 && in.hiChordRepeatRate != 4 && in.hiChordRepeatRate != 8) ||
        in.hiChordDrumKit >= HiChordDrumGrooves::KIT_COUNT ||
        in.hiChordGrooveStyle >= HiChordDrumGrooves::STYLE_COUNT ||
        in.hiChordGrooveVariation >= HiChordDrumGrooves::VARIATION_COUNT ||
        in.hiChordPracticeSong >= hiChordPracticeSongCount() || in.hiChordPracticePosition >= 16 ||
        in.hiChordEarLevel > 3 || in.medoScale >= MEDO_SCALE_COUNT ||
        in.medoArpDirection >= MEDO_ARP_COUNT ||
        (in.medoArpRate != 1 && in.medoArpRate != 2 && in.medoArpRate != 4 && in.medoArpRate != 8))
        return false;
    for (uint8_t i = 0; i < 16; ++i) {
        const HiChordSequenceStep &step = in.chordSequence[i];
        if (step.degree >= 7 || step.direction > CHORD_DIR_NW ||
            (step.slashDegree != 0xFF && step.slashDegree >= 7) || step.enabled > 1) return false;
    }
    for (uint8_t p = 0; p < 16; ++p) for (uint8_t s = 0; s < 16; ++s)
        if (in.poEffects[p][s] >= PO_FX_COUNT) return false;
    for (uint8_t role = 0; role < MEDO_ROLE_COUNT; ++role)
        if (in.medoTracks[role].volume > 127 || in.medoTracks[role].octave < -4 ||
            in.medoTracks[role].octave > 4 || in.medoTracks[role].quantize > MEDO_GROOVE) return false;
    MasterEffects validation;
    if (!validation.applySettings(in.masterEffects)) return false;
    Vocoder8Band vocoderValidation;
    if (!vocoderValidation.applySettings(in.vocoder)) return false;
    for (uint8_t preset = 0; preset < 4; ++preset) {
        const SavePerformancePreset &item = in.presets[preset];
        if (item.valid > 1) return false;
        if (!item.valid) continue;
        if (!validChord(item.chord)) return false;
        for (uint8_t synth = 0; synth < 3; ++synth)
            if (!synthProjectValidate(item.synths[synth])) return false;
    }
    return true;
}

bool performanceProjectDecode(const SavePerformanceState &in) {
    if (!performanceProjectValidate(in)) return false;
    g_chordSettings = decodeChord(in.chord); g_chordEngine.reset();
    g_hiChordPerformance.reset();
    g_hiChordPerformance.setMode(static_cast<HiChordMode>(in.hiChordMode));
    for (uint8_t i = 0; i < 16; ++i) g_hiChordPerformance.setSequenceStep(i, in.chordSequence[i]);
    g_poPatternEffects.clear();
    for (uint8_t p = 0; p < 16; ++p) for (uint8_t s = 0; s < 16; ++s)
        g_poPatternEffects.set(p, s, static_cast<PoEffect>(in.poEffects[p][s]));
    g_medoPerformance.reset(); g_medoPerformance.setRole(static_cast<MedoRole>(in.medoRole));
    for (uint8_t role = 0; role < MEDO_ROLE_COUNT; ++role) {
        const MedoRole r = static_cast<MedoRole>(role);
        g_medoPerformance.setVolume(r, in.medoTracks[role].volume);
        g_medoPerformance.setOctave(r, in.medoTracks[role].octave);
        g_medoPerformance.setQuantize(r, static_cast<MedoQuantize>(in.medoTracks[role].quantize));
    }
    g_masterEffects.reset();
    if (!g_masterEffects.applySettings(in.masterEffects)) return false;
    g_vocoder.reset();
    if (!g_vocoder.applySettings(in.vocoder)) return false;
    g_swing = in.swing;
    g_hiChordArpPattern = in.hiChordArpPattern;
    g_hiChordArpLayer = in.hiChordArpLayer;
    g_hiChordArpRate = in.hiChordArpRate;
    g_hiChordRepeatRate = in.hiChordRepeatRate;
    g_hiChordDrumKit = in.hiChordDrumKit;
    g_hiChordGrooveStyle = in.hiChordGrooveStyle;
    g_hiChordGrooveVariation = in.hiChordGrooveVariation;
    g_hiChordPracticeSong = in.hiChordPracticeSong;
    g_hiChordPracticePosition = in.hiChordPracticePosition;
    g_hiChordEarLevel = in.hiChordEarLevel;
    g_hiChordEarScore = in.hiChordEarScore;
    if (!g_medoPerformance.setScale(static_cast<MedoScale>(in.medoScale)) ||
        !g_medoPerformance.setArpDirection(static_cast<MedoArpDirection>(in.medoArpDirection)) ||
        !g_medoPerformance.setArpRate(in.medoArpRate) ||
        !g_medoPerformance.setSharedBars(in.medoSharedBars == 0 ? 128 : in.medoSharedBars)) return false;
    for (uint8_t preset = 0; preset < 4; ++preset) {
        g_performancePresets[preset].valid = in.presets[preset].valid;
        g_performancePresets[preset].chord = decodeChord(in.presets[preset].chord);
        memcpy(g_performancePresets[preset].synths, in.presets[preset].synths,
               sizeof(in.presets[preset].synths));
    }
    return true;
}
