// ============================================================
// Microgroove - input.cpp
// Snapshot diffing -> immediate / short / long press dispatch.
// All bindings live in keymap.h.
// Context-sensitive hold actions preserve the inherited mic/resample gestures
// and add streamed slot recording plus SONG-page master/stem recording.
// ============================================================
#include <M5Cardputer.h>
#include "config.h"
#include "keymap.h"
#include "sequencer.h"
#include "sampler.h"
#include "storage.h"
#include "wavetable.h"
#include "ui.h"
#include "mic_sampler.h"
#include "sd_diagnostics.h"
#include "loop_engine.h"
#include "sampler_slots.h"
#include "streaming_sampler.h"
#include "event_looper.h"
#include "motion.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include "midi_output.h"
#include "synth_parameters.h"
#include "synth_ui_model.h"
#include "performance_state.h"
#include "performance_scheduler_core.h"
#include "input.h"

#define LONG_PRESS_MS 450
#define HINT_AFTER_MS 140    // progress bar appears after this
#define RPT_DELAY_MS  350
#define RPT_RATE_MS    90

// ---------- snapshot of all 56 keys as synthetic codes ----------
struct KeySnap {
    uint8_t codes[16];
    uint8_t n = 0;
    void add(uint8_t kc) { if (n < sizeof(codes)) codes[n++] = kc; }
    bool has(uint8_t kc) const {
        for (uint8_t i = 0; i < n; i++) if (codes[i] == kc) return true;
        return false;
    }
};
static KeySnap s_prev;
uint8_t g_recPadKc = KC_NONE;          // key that ends mic capture on release

// pending short/long holds
struct Hold { uint32_t t0; uint8_t kc, act, fired; };
static Hold    s_holds[8];
static uint8_t s_nHolds = 0;

// auto-repeat state (one repeating key at a time is plenty)
static uint8_t  s_rptAct  = ACT_NONE;
static uint8_t  s_rptKc   = KC_NONE;
static uint32_t s_rptNext = 0;
static uint16_t s_rptCount = 0;

struct HeldMidiNote { uint8_t key, channel, note; bool audition; };
static HeldMidiNote s_heldMidiNotes[16];
static uint8_t s_heldMidiNoteCount = 0;

struct HeldChord {
    uint8_t key, degree, count, chordToneCount, bass;
    uint8_t note[7];
    // MIDI velocity and bass notes occupy seven bits. Their high bits carry
    // the MEDO arp/role-gain flags without growing each of seven held slots.
    uint8_t velocityAndMedoArp;
    uint32_t nextArpUs, arpTick;
};
static HeldChord s_heldChords[7];
static uint8_t s_heldChordCount = 0;
static PerformanceGateRelease s_chordReleases[21];
static uint8_t s_chordReleaseCount = 0;
struct HeldAutoDrum {
    uint32_t nextUs;
    uint8_t key, lane, rate, tick;
};
static_assert(sizeof(Hold) == 8, "key-hold SRAM layout changed");
static_assert(sizeof(HeldChord) == 24, "held chord SRAM layout changed");
static_assert(sizeof(HeldAutoDrum) == 8, "auto-drum SRAM layout changed");
static HeldAutoDrum s_autoDrums[7];
static uint8_t s_autoDrumCount = 0;
static uint8_t s_sampleCopySlot = 0xFF;
static uint8_t s_sampleCopySlice = 0xFF;
static uint8_t s_selectedSampleSlice = 0;
static uint8_t s_poEffectKey = KC_NONE;
static uint32_t s_hiroStartUs = 0;
static uint16_t s_hiroScore = 0;
static uint8_t s_earDegrees[4] = {};
static uint8_t s_earDirections[4] = {};
static uint8_t s_earCount = 1, s_earPosition = 0;
static uint32_t s_earRandom = 1;
static uint32_t s_earAuditionStartUs = 0;
static uint8_t s_earAuditionNext = 0, s_earAuditionEnd = 0;
static bool s_earAuditionRootOnly = false;
static uint8_t s_previousLoopState[LOOP_STREAM_TRACKS] = {};

uint16_t inputChordHiroScore() { return s_hiroScore; }

static uint8_t heldVelocity(const HeldChord &held) {
    return static_cast<uint8_t>(held.velocityAndMedoArp & 0x7Fu);
}
static bool heldMedoArp(const HeldChord &held) {
    return (held.velocityAndMedoArp & 0x80u) != 0;
}
static bool heldMedoRoleGain(const HeldChord &held) {
    return (held.bass & 0x80u) != 0;
}
static uint8_t heldRole(const HeldChord &held, uint8_t index) {
    return heldMedoRoleGain(held) ? static_cast<uint8_t>(EVENT_ROLE_CHORD) :
        static_cast<uint8_t>(index < held.chordToneCount
            ? EVENT_ROLE_CHORD : EVENT_ROLE_BASS);
}
static uint8_t heldTrack(const HeldChord &held, uint8_t index) {
    return index < held.chordToneCount ? static_cast<uint8_t>(index / 3u) : 2u;
}

static int8_t chordKeyDegree(uint8_t key) {
    static const uint8_t keys[7] = {KC_FN, KC_SHIFT, 'a', 's', 'd', 'f', 'g'};
    for (uint8_t i = 0; i < 7; ++i) if (keys[i] == key) return i;
    return -1;
}

static ChordDirection heldChordDirection(const KeySnap &keys) {
    const bool north = keys.has('v'), south = keys.has('c');
    const bool west = keys.has('x'), east = keys.has('b');
    if (north && east) return CHORD_DIR_NE;
    if (south && east) return CHORD_DIR_SE;
    if (south && west) return CHORD_DIR_SW;
    if (north && west) return CHORD_DIR_NW;
    if (north) return CHORD_DIR_N;
    if (east) return CHORD_DIR_E;
    if (south) return CHORD_DIR_S;
    if (west) return CHORD_DIR_W;
    return CHORD_DIR_CENTER;
}

static void fireChordMidi(HeldChord &held, uint8_t index) {
    if (index >= held.count) return;
    const uint8_t midi = held.note[index], track = heldTrack(held, index);
    eventLooperSetRecordRoleOverride(heldRole(held, index));
    eventLooperSetRecordRoleGain(heldMedoRoleGain(held));
    liveSynthNote(track, static_cast<uint8_t>(midi % 12u + 1u),
                  static_cast<uint8_t>(midi / 12u - 1u), false, index > 0,
                  heldVelocity(held));
    eventLooperSetRecordRoleOverride(-1);
    eventLooperSetRecordRoleGain(false);
}

static void scheduleChordRelease(const HeldChord &held, uint8_t index,
                                 uint32_t dueUs) {
    if (index >= held.count) return;
    for (uint8_t i = 0; i < s_chordReleaseCount; ++i) {
        PerformanceGateRelease &pending = s_chordReleases[i];
        if (performanceGateTrack(pending) == heldTrack(held, index) &&
            performanceGateNote(pending) == held.note[index]) {
            pending = performanceGateMake(heldTrack(held, index), held.note[index],
                                         heldRole(held, index), false, dueUs);
            return;
        }
    }
    if (s_chordReleaseCount < sizeof(s_chordReleases) / sizeof(s_chordReleases[0]))
        s_chordReleases[s_chordReleaseCount++] = performanceGateMake(
            heldTrack(held, index), held.note[index], heldRole(held, index), false, dueUs);
}

static void processChordReleases(uint32_t nowUs) {
    for (uint8_t i = 0; i < s_chordReleaseCount;) {
        const PerformanceGateRelease pending = s_chordReleases[i];
        if (!performanceGateDue(pending, nowUs)) { ++i; continue; }
        const uint8_t track = performanceGateTrack(pending);
        const uint8_t note = performanceGateNote(pending);
        if (performanceGateAudition(pending)) {
            g_synths[track].noteOff(note);
            midiOutputNoteOff(track, note);
        } else {
            eventLooperSetRecordRoleOverride(performanceGateRole(pending));
            liveSynthRelease(track, note);
            eventLooperSetRecordRoleOverride(-1);
        }
        s_chordReleases[i] = s_chordReleases[--s_chordReleaseCount];
    }
}

void inputStopHiChordPerformanceNotes() {
    for (uint8_t chord = 0; chord < s_heldChordCount; ++chord) {
        HeldChord &held = s_heldChords[chord];
        for (uint8_t note = 0; note < held.count; ++note) {
            eventLooperSetRecordRoleOverride(heldRole(held, note));
            liveSynthRelease(heldTrack(held, note), held.note[note]);
        }
    }
    eventLooperSetRecordRoleOverride(-1);
    for (uint8_t track = 0; track < NUM_SYNTHS; ++track) g_synths[track].hardStop();
    s_heldChordCount = 0;
    s_chordReleaseCount = 0;
    s_autoDrumCount = 0;
}

static void applyHiChordDrumKit(uint8_t kit) {
    g_hiChordDrumKit = kit % HiChordDrumGrooves::KIT_COUNT;
    if (g_hiChordDrumKit == 6) return; // user kit preserves lane assignments
    for (uint8_t lane = 0; lane < 7; ++lane) {
        DrumLane &drum = g_drumLanes[lane];
        drum.engine = (g_hiChordDrumKit == 2 || g_hiChordDrumKit == 5) ? ENG_909 : ENG_808;
        drum.type = static_cast<uint8_t>(lane % DT_COUNT);
        drum.tune = g_hiChordDrumKit == 5 && lane == 0 ? -5.0f : 0.0f;
        drum.decay = g_hiChordDrumKit == 3 ? 1.4f : g_hiChordDrumKit == 4 ? 0.75f : 1.0f;
    }
}

static void writeHiChordGroove(uint8_t style, uint8_t variation) {
    g_hiChordGrooveStyle = style % HiChordDrumGrooves::STYLE_COUNT;
    g_hiChordGrooveVariation = variation % HiChordDrumGrooves::VARIATION_COUNT;
    for (uint8_t step = 0; step < NUM_STEPS; ++step) {
        uint8_t mask = 0;
        for (uint8_t voice = 0; voice < HiChordDrumGrooves::VOICE_COUNT; ++voice)
            if (HiChordDrumGrooves::hit(g_hiChordGrooveStyle, g_hiChordGrooveVariation,
                                        voice, step)) mask |= static_cast<uint8_t>(1u << voice);
        g_patterns[g_curPattern].drums[step] = mask;
    }
}

static bool armHiChordBounce(HiChordBounceSource source) {
    const LoopEngineSnapshot loops = loopEngineSnapshot();
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        if (loops.tracks[track].state != LOOP_STREAM_EMPTY) continue;
        if (!loopEngineRequestRecord(track)) continue;
        g_hiChordBounceActive = true;
        g_hiChordBounceTrack = track;
        g_hiChordBounceSource = source;
        if (!g_playing) sequencerStart(true);
        uiStatus(source == HICHORD_BOUNCE_DRONE ? "DRONE BOUNCE ARMED" :
                 source == HICHORD_BOUNCE_DRUM_LOOP ? "DRUM BOUNCE ARMED" :
                 "SEQ BOUNCE ARMED");
        return true;
    }
    return false;
}

static bool requestLoopRecord(uint8_t track) {
    if (track != 0 || g_hiChordLoopBars == 0)
        return loopEngineRequestRecord(track);
    const uint32_t target = hiChordLoopFrames(g_hiChordLoopBars, g_bpm, SAMPLE_RATE);
    const uint32_t countIn = hiChordCountInFrames(g_bpm, SAMPLE_RATE);
    return target != 0 && loopEngineRequestRecord(track, target, countIn);
}

static void startChordHiro() {
    g_hiChordPracticePosition = 0;
    s_hiroScore = 0;
    const uint32_t beat = 60000000UL / (g_bpm ? g_bpm : 120u);
    s_hiroStartUs = micros() + beat; // one-beat ready count
    uiStatus("CHORD HIRO: GET READY");
}

static void updateChordHiro(uint32_t nowUs) {
    if (!s_hiroStartUs || g_hiChordPerformance.mode() != HICHORD_CHORD_HIRO)
        return;
    const HiChordPracticeSong& song = hiChordPracticeSong(g_hiChordPracticeSong);
    const uint32_t beat = 60000000UL / (g_bpm ? g_bpm : 120u);
    const uint8_t difficulty = min<uint8_t>(3, g_hiChordPracticeSong / 4u);
    while (g_hiChordPracticePosition < song.length) {
        const uint32_t due = s_hiroStartUs +
            static_cast<uint32_t>(g_hiChordPracticePosition) * beat;
        const uint32_t lateUs = static_cast<uint32_t>(
            hiChordHiroWindowMs(difficulty)) * 1000u;
        if (static_cast<int32_t>(nowUs - (due + lateUs)) <= 0) break;
        ++g_hiChordPracticePosition;
    }
    if (g_hiChordPracticePosition >= song.length) {
        s_hiroStartUs = 0;
        uiStatus("CHORD HIRO COMPLETE");
        g_needRedraw = true;
    }
}

static uint32_t nextEarRandom() {
    s_earRandom = s_earRandom * 1664525u + 1013904223u;
    return s_earRandom;
}

static void stopEarAudition() {
    s_earAuditionStartUs = 0;
    for (uint8_t index = 0; index < s_chordReleaseCount;) {
        const PerformanceGateRelease pending = s_chordReleases[index];
        if (!performanceGateAudition(pending)) { ++index; continue; }
        const uint8_t track = performanceGateTrack(pending);
        const uint8_t note = performanceGateNote(pending);
        g_synths[track].noteOff(note);
        midiOutputNoteOff(track, note);
        s_chordReleases[index] = s_chordReleases[--s_chordReleaseCount];
    }
}

static void queueEarAudition(bool rootHint = false) {
    stopEarAudition();
    s_earAuditionNext = rootHint ? s_earPosition : 0;
    s_earAuditionEnd = static_cast<uint8_t>(
        s_earAuditionNext + (rootHint ? 1 : s_earCount));
    s_earAuditionRootOnly = rootHint;
    s_earAuditionStartUs = micros();
}

static void startEarRound() {
    s_earCount = (g_hiChordEarLevel & 1u) ? 4 : 1;
    s_earPosition = 0;
    for (uint8_t index = 0; index < s_earCount; ++index) {
        s_earDegrees[index] = static_cast<uint8_t>(nextEarRandom() % 7u);
        s_earDirections[index] = g_hiChordEarLevel >= 2
            ? static_cast<uint8_t>(1u + nextEarRandom() % 8u)
            : static_cast<uint8_t>(CHORD_DIR_CENTER);
    }
    g_hiChordEarTarget = s_earDegrees[0];
    queueEarAudition(false);
    uiStatus(s_earCount == 1 ? "LISTEN: ONE CHORD" : "LISTEN: 4 CHORDS");
}

static void updateEarAudition(uint32_t nowUs) {
    if (!s_earAuditionStartUs || s_earAuditionNext >= s_earAuditionEnd) return;
    const uint8_t sequenceOffset = static_cast<uint8_t>(
        s_earAuditionNext - (s_earAuditionRootOnly ? s_earPosition : 0));
    const uint32_t due = s_earAuditionStartUs +
        static_cast<uint32_t>(sequenceOffset) * 500000u;
    if (static_cast<int32_t>(nowUs - due) < 0) return;
    const uint8_t targetIndex = s_earAuditionNext++;
    const ChordDirection direction = g_hiChordEarLevel >= 2
        ? static_cast<ChordDirection>(s_earDirections[targetIndex])
        : CHORD_DIR_CENTER;
    const ChordVoicing chord = g_chordEngine.build(
        g_chordSettings, s_earDegrees[targetIndex], direction);
    const uint8_t noteCount = s_earAuditionRootOnly ? 1 : chord.count;
    g_synths[0].setVoices(3); g_synths[1].setVoices(3);
    for (uint8_t noteIndex = 0; noteIndex < noteCount; ++noteIndex) {
        const uint8_t midi = chord.notes[noteIndex];
        const uint8_t track = static_cast<uint8_t>(noteIndex / 3u);
        g_synths[track].noteOnLive(
            noteToFreq(static_cast<uint8_t>(midi % 12u + 1u),
                       static_cast<uint8_t>(midi / 12u - 1u)),
            false, false, midi, 96);
        midiOutputNoteOn(track, midi, 96);
        if (s_chordReleaseCount < sizeof(s_chordReleases) /
                                      sizeof(s_chordReleases[0]))
            s_chordReleases[s_chordReleaseCount++] = performanceGateMake(
                track, midi, EVENT_ROLE_CHORD, true, nowUs + 350000u);
    }
    if (s_earAuditionNext >= s_earAuditionEnd) s_earAuditionStartUs = 0;
}

static void updateLoopAutoAdvance() {
    const LoopEngineSnapshot loops = loopEngineSnapshot();
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        const LoopStreamState previous = static_cast<LoopStreamState>(
            s_previousLoopState[track]);
        const LoopStreamState current = loops.tracks[track].state;
        if ((previous == LOOP_STREAM_RECORDING || previous == LOOP_STREAM_FINALIZING) &&
            (current == LOOP_STREAM_PLAYING || current == LOOP_STREAM_MUTED)) {
            for (uint8_t offset = 1; offset < LOOP_STREAM_TRACKS; ++offset) {
                const uint8_t candidate = static_cast<uint8_t>(
                    (track + offset) % LOOP_STREAM_TRACKS);
                if (loops.tracks[candidate].state == LOOP_STREAM_EMPTY) {
                    g_loopCursor = candidate;
                    break;
                }
            }
        }
        s_previousLoopState[track] = static_cast<uint8_t>(current);
    }
}

static void playChord(uint8_t key, const KeySnap &keys, uint8_t velocity = 104,
                      int8_t performanceOctave = 0, bool medoArp = false,
                      int8_t roleOverride = -1) {
    const int8_t degree = chordKeyDegree(key);
    if (degree < 0) return;
    g_chordDegree = static_cast<uint8_t>(degree);
    const HiChordMode mode = roleOverride >= 0
        ? HICHORD_PLAY : g_hiChordPerformance.mode();
    if (mode == HICHORD_DRUM) { liveDrumHit(static_cast<uint8_t>(degree)); return; }
    if (mode == HICHORD_DRUM_LOOPS) {
        writeHiChordGroove(static_cast<uint8_t>(degree), g_hiChordGrooveVariation);
        uiStatus("GROOVE LOADED"); g_needRedraw = true; return;
    }
    if (mode == HICHORD_AUTO_DRUM) {
        const ChordDirection direction = heldChordDirection(keys);
        liveDrumHit(static_cast<uint8_t>(degree));
        if (direction != CHORD_DIR_CENTER && s_autoDrumCount < 7) {
            HeldAutoDrum &held = s_autoDrums[s_autoDrumCount++];
            held.key = key; held.lane = static_cast<uint8_t>(degree);
            held.rate = static_cast<uint8_t>(hiChordAutoDrumRate(direction));
            held.tick = 1;
            held.nextUs = micros() + hiChordRateIntervalUs(
                static_cast<HiChordRate>(held.rate), g_bpm, 0);
            uiStatus("AUTO DRUM HELD");
        }
        return;
    }
    if (mode == HICHORD_MIC_SAMPLE) {
        liveSampleHit(g_streamSampleSlot, static_cast<uint8_t>(degree)); return;
    }
    if (mode == HICHORD_MIXER) {
        if (degree < 6) {
            const LoopStreamState state = loopEngineSnapshot().tracks[degree].state;
            loopEngineSetMuted(static_cast<uint8_t>(degree), state != LOOP_STREAM_MUTED);
        } else {
            const LoopEngineSnapshot loops = loopEngineSnapshot();
            loopEngineSetMetronome(!loops.metronome);
        }
        g_needRedraw = true; return;
    }
    if (mode == HICHORD_TUNER) { uiStatus("HOLD AUX FOR TUNER INPUT"); return; }
    if (mode == HICHORD_EAR_TRAINER) {
        const bool degreeCorrect = static_cast<uint8_t>(degree) == g_hiChordEarTarget;
        const bool directionCorrect = g_hiChordEarLevel < 2 ||
            heldChordDirection(keys) ==
                static_cast<ChordDirection>(s_earDirections[s_earPosition]);
        if (degreeCorrect && directionCorrect) {
            ++g_hiChordEarScore;
            ++s_earPosition;
            if (s_earPosition >= s_earCount) {
                uiStatus("ROUND CORRECT!");
                startEarRound();
            } else {
                g_hiChordEarTarget = s_earDegrees[s_earPosition];
                uiStatus("CORRECT - NEXT");
            }
        } else {
            uiStatus(directionCorrect ? "TRY ANOTHER CHORD" : "WRONG DIRECTION");
        }
    }
    if (mode == HICHORD_CHORD_HIRO) {
        const HiChordPracticeSong &song = hiChordPracticeSong(g_hiChordPracticeSong);
        if (!s_hiroStartUs) {
            uiStatus("PRESS PLAY TO START");
        } else {
            const uint32_t beat = 60000000UL / (g_bpm ? g_bpm : 120u);
            const uint32_t due = s_hiroStartUs +
                static_cast<uint32_t>(g_hiChordPracticePosition) * beat;
            const int32_t errorMs = static_cast<int32_t>(micros() - due) / 1000;
            const uint8_t difficulty = min<uint8_t>(3, g_hiChordPracticeSong / 4u);
            const HiChordHiroGrade grade = hiChordHiroGrade(
                song.degrees[g_hiChordPracticePosition], static_cast<uint8_t>(degree),
                errorMs, difficulty);
            if (grade != HICHORD_HIRO_MISS)
                s_hiroScore = static_cast<uint16_t>(s_hiroScore + grade * 25u);
            ++g_hiChordPracticePosition;
            uiStatus(hiChordHiroGradeName(grade));
            if (g_hiChordPracticePosition >= song.length) s_hiroStartUs = 0;
        }
    }
    // Lead is strictly monophonic and Drone is a single latched chord. A new
    // button replaces the previous sound instead of accumulating hidden held
    // notes until the seven-entry array fills.
    if (mode == HICHORD_LEAD || mode == HICHORD_DRONE)
        inputStopHiChordPerformanceNotes();
    if (s_heldChordCount >= 7) return;
    const ChordDirection direction = heldChordDirection(keys);
    ChordSettings buildSettings = g_chordSettings;
    buildSettings.octave = static_cast<int8_t>(constrain(
        static_cast<int>(buildSettings.octave) + performanceOctave, 1, 7));
    if (mode == HICHORD_LEAD) buildSettings.bassMode = CHORD_BASS_OFF;
    uint8_t slashDegree = 0xFF;
    if (buildSettings.bassMode == CHORD_BASS_SLASH && s_heldChordCount)
        slashDegree = s_heldChords[0].degree;
    ChordVoicing voicing = g_chordEngine.build(
        buildSettings, static_cast<uint8_t>(degree), direction, slashDegree);
    if (mode == HICHORD_LEAD) { voicing.count = 1; voicing.chordToneCount = 1; }
    HeldChord &held = s_heldChords[s_heldChordCount++];
    held.key = key; held.degree = static_cast<uint8_t>(degree);
    held.count = voicing.count; held.chordToneCount = voicing.chordToneCount;
    held.bass = static_cast<uint8_t>((voicing.bass & 0x7Fu) |
        (roleOverride >= 0 ? 0x80u : 0u));
    held.velocityAndMedoArp = static_cast<uint8_t>((velocity & 0x7Fu) |
        (medoArp ? 0x80u : 0u));
    held.arpTick = 0; held.nextArpUs = micros();
    for (uint8_t i = 0; i < voicing.count; ++i) {
        held.note[i] = voicing.notes[i];
    }
    if (g_recEnabled || g_hiChordPerformance.mode() == HICHORD_SEQUENCER) {
        const HiChordSequenceStep sequence = {static_cast<uint8_t>(degree),
            static_cast<uint8_t>(direction), slashDegree, 1};
        g_hiChordPerformance.setSequenceStep(g_curStep, sequence);
        if (g_hiChordPerformance.mode() == HICHORD_SEQUENCER)
            g_curStep = static_cast<uint8_t>((g_curStep + 1u) %
                                             g_hiChordSequenceLength);
    }
    g_synths[0].setVoices(3); g_synths[1].setVoices(3);
    if (medoArp) {
        held.nextArpUs = micros();
    } else if (mode == HICHORD_STRUM) {
        HiChordScheduledNote scheduled[7];
        const uint16_t spacing = hiChordStrumSpacingFrames(
            static_cast<HiChordStrumSpeed>(g_hiChordStrumSpeed), SAMPLE_RATE);
        g_hiChordPerformance.scheduleStrum(voicing, false, spacing, scheduled);
        for (uint8_t i = 0; i < held.count; ++i) held.note[i] = scheduled[i].note;
        fireChordMidi(held, 0); held.arpTick = 1;
        held.nextArpUs += static_cast<uint32_t>(spacing) * 1000000u / SAMPLE_RATE;
    } else if (mode == HICHORD_ARPEGGIO && g_hiChordArpLayer == ARP_CHORD_PLUS) {
        for (uint8_t i = 0; i < held.count; ++i) fireChordMidi(held, i);
    } else if (mode != HICHORD_ARPEGGIO && mode != HICHORD_REPEAT) {
        for (uint8_t i = 0; i < held.count; ++i) fireChordMidi(held, i);
    }
    g_needRedraw = true;
}

static void releaseChord(uint8_t key) {
    for (uint8_t index = 0; index < s_autoDrumCount; ++index)
        if (s_autoDrums[index].key == key) {
            s_autoDrums[index] = s_autoDrums[--s_autoDrumCount];
            return;
        }
    for (uint8_t index = 0; index < s_heldChordCount; ++index) {
        HeldChord &held = s_heldChords[index];
        if (held.key != key) continue;
        if (heldMedoArp(held) || g_hiChordPerformance.mode() != HICHORD_DRONE)
            for (uint8_t i = 0; i < held.count; ++i) {
                eventLooperSetRecordRoleOverride(heldRole(held, i));
                liveSynthRelease(heldTrack(held, i), held.note[i]);
            }
        eventLooperSetRecordRoleOverride(-1);
        held = s_heldChords[--s_heldChordCount];
        return;
    }
}

static void updateHeldChords() {
    const uint32_t now = micros();
    processChordReleases(now);
    for (uint8_t drum = 0; drum < s_autoDrumCount; ++drum) {
        HeldAutoDrum &held = s_autoDrums[drum];
        if (static_cast<int32_t>(now - held.nextUs) < 0) continue;
        liveDrumHit(held.lane);
        held.nextUs += hiChordRateIntervalUs(
            static_cast<HiChordRate>(held.rate), g_bpm, held.tick++);
    }
    for (uint8_t h = 0; h < s_heldChordCount; ++h) {
        HeldChord &held = s_heldChords[h];
        if (g_hiChordPerformance.mode() == HICHORD_DRONE) {
            for (uint8_t track = 0; track < NUM_SYNTHS; ++track)
                g_synths[track].sustainLegacy();
        }
        if (static_cast<int32_t>(now - held.nextArpUs) < 0) continue;
        const HiChordMode mode = g_hiChordPerformance.mode();
        if (heldMedoArp(held)) {
            const uint32_t interval = g_medoPerformance.arpIntervalUs(g_bpm);
            if (interval == 0) continue;
            const uint8_t note = g_medoPerformance.arpNoteIndex(
                held.chordToneCount ? held.chordToneCount : held.count, held.arpTick);
            fireChordMidi(held, note);
            scheduleChordRelease(held, note, now + min<uint32_t>(
                200000u, interval * 3u / 4u));
            ++held.arpTick;
            held.nextArpUs += interval;
        } else if (mode == HICHORD_STRUM && held.arpTick < held.count) {
            fireChordMidi(held, static_cast<uint8_t>(held.arpTick++));
            const uint16_t spacing = hiChordStrumSpacingFrames(
                static_cast<HiChordStrumSpeed>(g_hiChordStrumSpeed), SAMPLE_RATE);
            held.nextArpUs += static_cast<uint32_t>(spacing) * 1000000u / SAMPLE_RATE;
        } else if (mode == HICHORD_ARPEGGIO || mode == HICHORD_REPEAT) {
            const HiChordRate rate = static_cast<HiChordRate>(
                mode == HICHORD_ARPEGGIO ? g_hiChordArpRate : g_hiChordRepeatRate);
            const uint32_t interval = hiChordRateIntervalUs(rate, g_bpm, held.arpTick);
            const uint32_t gate = interval < 266666u ? interval * 3u / 4u : 200000u;
            if (mode == HICHORD_REPEAT) {
                for (uint8_t note = 0; note < held.count; ++note) {
                    fireChordMidi(held, note);
                    scheduleChordRelease(held, note, now + gate);
                }
                ++held.arpTick; held.nextArpUs += interval; continue;
            } else {
                if (g_hiChordArpLayer == ARP_RHYTHM_PLUS)
                    for (uint8_t note = 0; note < held.count; ++note) {
                        fireChordMidi(held, note);
                        scheduleChordRelease(held, note, now + gate);
                    }
                ChordVoicing voicing = {};
                voicing.count = held.count; voicing.chordToneCount = held.chordToneCount;
                voicing.bass = static_cast<uint8_t>(held.bass & 0x7Fu);
                for (uint8_t note = 0; note < held.count; ++note) voicing.notes[note] = held.note[note];
                uint8_t selected[2] = {};
                const uint8_t selectedCount = g_hiChordPerformance.arpNotes(
                    voicing, static_cast<HiChordArpPattern>(g_hiChordArpPattern),
                    held.arpTick, selected);
                for (uint8_t selectedIndex = 0; selectedIndex < selectedCount; ++selectedIndex)
                    for (uint8_t note = 0; note < held.count; ++note)
                        if (held.note[note] == selected[selectedIndex]) {
                            fireChordMidi(held, note);
                            if (g_hiChordArpLayer != ARP_CHORD_PLUS)
                                scheduleChordRelease(held, note, now + gate);
                            break;
                        }
            }
            ++held.arpTick;
            held.nextArpUs += interval;
        }
    }
}

static void outputMedoGesture(MedoGesture gesture, uint8_t value) {
    const MedoMidiGesture midi = MedoPerformance::gestureMidi(gesture, value, 15);
    const uint8_t message[3] = {midi.status, midi.data1, midi.data2};
    midiOutputMessage(message, (midi.status & 0xF0u) == 0xD0u ? 2 : 3);
}

// ---------- helpers ----------
static bool accentHeld(const KeySnap& s) { return s.has('m'); }

static void rememberMidiNote(uint8_t key, uint8_t channel, uint8_t note,
                             bool audition = false) {
    for (uint8_t index = 0; index < s_heldMidiNoteCount; ++index) {
        if (s_heldMidiNotes[index].key != key) continue;
        s_heldMidiNotes[index] = {key, channel, note, audition};
        return;
    }
    if (s_heldMidiNoteCount < sizeof(s_heldMidiNotes) / sizeof(s_heldMidiNotes[0]))
        s_heldMidiNotes[s_heldMidiNoteCount++] = {key, channel, note, audition};
}

static void releaseMidiNote(uint8_t key) {
    for (uint8_t index = 0; index < s_heldMidiNoteCount; ++index) {
        if (s_heldMidiNotes[index].key != key) continue;
        if (s_heldMidiNotes[index].audition) {
            // P3 (reconciliation report): SOUND-page auditions never record
            // their note-on, so recording the release would write orphan
            // note-off events into an armed event-looper track. Release the
            // voice and mirror MIDI out, but skip the event record.
            g_synths[s_heldMidiNotes[index].channel].noteOff(
                s_heldMidiNotes[index].note);
            midiOutputNoteOff(s_heldMidiNotes[index].channel,
                              s_heldMidiNotes[index].note);
        } else {
            liveSynthRelease(s_heldMidiNotes[index].channel,
                             s_heldMidiNotes[index].note);
        }
        s_heldMidiNotes[index] = s_heldMidiNotes[--s_heldMidiNoteCount];
        return;
    }
}

static uint8_t heldPianoCount(const KeySnap& s) {
    uint8_t c = 0;
    for (uint8_t i = 0; i < s.n; i++) if (pianoSemi(s.codes[i]) >= 0) c++;
    return c;
}

static void semiToNote(int8_t semi, uint8_t& note, uint8_t& oct) {
    note = (uint8_t)(semi % 12) + 1;
    oct  = (uint8_t)constrain((int)g_curOctave + semi / 12, 1, 7);
}

static int8_t samplePerformanceKey(uint8_t kc) {
    static const uint8_t keys[SAMPLER_SLICE_COUNT] = {
        KC_FN, KC_SHIFT, 'a', 's', 'd', 'f', 'g', 'h',
        'j', 'k', 'l', ';', '\'', KC_ENTER, 'q', 'w'
    };
    for (uint8_t index = 0; index < SAMPLER_SLICE_COUNT; ++index)
        if (keys[index] == kc) return static_cast<int8_t>(index);
    return -1;
}

static SamplerLockEntry editableSampleLock() {
    const SamplerLockEntry* existing = g_samplerSequence.findLock(
        g_curPattern, g_curStep, g_streamSampleSlot);
    if (existing) return *existing;
    const SamplerSlot& slot = g_samplerSlotBank.slot(g_streamSampleSlot);
    SamplerLockEntry result = {};
    result.pattern = g_curPattern;
    result.step = g_curStep;
    result.slot = g_streamSampleSlot;
    result.gainQ15 = slot.gainQ15;
    result.cutoffQ15 = slot.cutoffQ15;
    result.resonanceQ15 = slot.resonanceQ15;
    result.trimLengthQ15 = 32767;
    return result;
}

static void adjustSampleParameter(int direction) {
    SamplerSlot& slot = g_samplerSlotBank.slot(g_streamSampleSlot);
    if (slot.mode == SAMPLER_SLOT_EMPTY) { uiStatus("EMPTY SLOT"); return; }
    if (g_sampleEditMode == 1) {
        // Direct-field edits hold the slot seqlock so a concurrent trigger
        // snapshot on another task can never observe a torn struct (P3).
        switch (g_sampleParam) {
            case 0:
                g_samplerSlotBank.beginEdit(g_streamSampleSlot);
                slot.pitchQ8 = static_cast<int16_t>(constrain(
                    static_cast<int>(slot.pitchQ8) + direction * 256, -24 * 256, 24 * 256));
                g_samplerSlotBank.endEdit(g_streamSampleSlot);
                break;
            case 1:
                g_samplerSlotBank.beginEdit(g_streamSampleSlot);
                slot.gainQ15 = static_cast<uint16_t>(constrain(
                    static_cast<int>(slot.gainQ15) + direction * 2048, 0, 32767));
                g_samplerSlotBank.endEdit(g_streamSampleSlot);
                break;
            case 2:
                g_samplerSlotBank.beginEdit(g_streamSampleSlot);
                slot.cutoffQ15 = static_cast<uint16_t>(constrain(
                    static_cast<int>(slot.cutoffQ15) + direction * 2048, 0, 32767));
                g_samplerSlotBank.endEdit(g_streamSampleSlot);
                break;
            case 3:
                g_samplerSlotBank.beginEdit(g_streamSampleSlot);
                slot.resonanceQ15 = static_cast<uint16_t>(constrain(
                    static_cast<int>(slot.resonanceQ15) + direction * 2048, 0, 32767));
                g_samplerSlotBank.endEdit(g_streamSampleSlot);
                break;
            case 4: {
                if (slot.mode == SAMPLER_SLOT_SLICED) {
                    const SamplerRegion region = slot.slices[s_selectedSampleSlice];
                    const uint32_t trimEnd = slot.trimStart + slot.trimLength;
                    const uint32_t maximum = trimEnd - region.lengthFrames;
                    const uint32_t next = static_cast<uint32_t>(constrain(
                        static_cast<int>(region.startFrame) + direction * 128,
                        static_cast<int>(slot.trimStart), static_cast<int>(maximum)));
                    g_samplerSlotBank.setSlice(g_streamSampleSlot, s_selectedSampleSlice,
                                               next, region.lengthFrames);
                } else {
                    const uint32_t amount = min<uint32_t>(256, slot.sourceFrames);
                    const int next = constrain(static_cast<int>(slot.trimStart) +
                                               direction * static_cast<int>(amount),
                                               0, static_cast<int>(slot.sourceFrames - SAMPLER_SLICE_COUNT));
                    const uint32_t length = min<uint32_t>(slot.trimLength,
                                                          slot.sourceFrames - next);
                    g_samplerSlotBank.setTrim(g_streamSampleSlot, next,
                                              max<uint32_t>(SAMPLER_SLICE_COUNT, length));
                }
                break;
            }
            case 5: {
                if (slot.mode == SAMPLER_SLOT_SLICED) {
                    const SamplerRegion region = slot.slices[s_selectedSampleSlice];
                    const uint32_t maximum = slot.trimStart + slot.trimLength - region.startFrame;
                    const uint32_t next = static_cast<uint32_t>(constrain(
                        static_cast<int>(region.lengthFrames) + direction * 128,
                        2, static_cast<int>(maximum)));
                    g_samplerSlotBank.setSlice(g_streamSampleSlot, s_selectedSampleSlice,
                                               region.startFrame, next);
                } else {
                    const int next = constrain(static_cast<int>(slot.trimLength) + direction * 256,
                                               static_cast<int>(SAMPLER_SLICE_COUNT),
                                               static_cast<int>(slot.sourceFrames - slot.trimStart));
                    g_samplerSlotBank.setTrim(g_streamSampleSlot, slot.trimStart, next);
                }
                break;
            }
        }
        return;
    }

    SamplerLockEntry lock = editableSampleLock();
    switch (g_sampleParam) {
        case 0:
            lock.pitchQ8 = static_cast<int16_t>(constrain(
                static_cast<int>(lock.pitchQ8) + direction * 256, -24 * 256, 24 * 256));
            lock.flags |= SAMPLER_LOCK_PITCH;
            break;
        case 1:
            lock.gainQ15 = static_cast<uint16_t>(constrain(
                static_cast<int>(lock.gainQ15) + direction * 2048, 0, 32767));
            lock.flags |= SAMPLER_LOCK_GAIN;
            break;
        case 2:
            lock.cutoffQ15 = static_cast<uint16_t>(constrain(
                static_cast<int>(lock.cutoffQ15) + direction * 2048, 0, 32767));
            lock.flags |= SAMPLER_LOCK_FILTER;
            break;
        case 3:
            lock.resonanceQ15 = static_cast<uint16_t>(constrain(
                static_cast<int>(lock.resonanceQ15) + direction * 2048, 0, 32767));
            lock.flags |= SAMPLER_LOCK_FILTER;
            break;
        case 4:
            lock.trimStartQ15 = static_cast<uint16_t>(constrain(
                static_cast<int>(lock.trimStartQ15) + direction * 2048, 0, 30720));
            lock.flags |= SAMPLER_LOCK_TRIM;
            break;
        case 5:
            lock.trimLengthQ15 = static_cast<uint16_t>(constrain(
                static_cast<int>(lock.trimLengthQ15) + direction * 2048, 2048, 32767));
            lock.flags |= SAMPLER_LOCK_TRIM;
            break;
    }
    if (!g_samplerSequence.setLock(lock)) uiStatus("LOCK TABLE FULL");
}

// ---------- SOUND page parameter model ----------
#define DRUM_PARAMS  7

static void adjustSynthParam(SynthTrack& track, uint8_t row, int direction, bool fine) {
    const SynthUiRow item = synthSoundBankRow(g_soundBank, row);
    int32_t value = 0;
    if (item.parameter >= SYNTH_PARAM_COUNT ||
        !synthGetParameter(track, item.parameter, value)) return;
    const int32_t step = synthSoundParameterStep(item.parameter, fine);
    int32_t next = value + direction * step;
    int32_t wrap = 0;
    if (item.parameter == SYNTH_PARAM_ENGINE) wrap = SYNTH_ENGINE_COUNT;
    else if (item.parameter == SYNTH_PARAM_MG_OSC ||
             item.parameter == SYNTH_PARAM_MGX_OSC) wrap = OSC_COUNT;
    else if (item.parameter == SYNTH_PARAM_MG_WAVETABLE ||
             item.parameter == SYNTH_PARAM_MGX_WAVETABLE)
        wrap = g_numWavetables;
    else if (item.parameter == SYNTH_PARAM_MGX_FILTER_MODE) wrap = SYNTH_FILTER_COUNT;
    else if (item.parameter == SYNTH_PARAM_MGX_LFO_DESTINATION) wrap = SYNTH_LFO_COUNT;
    else if (item.parameter == SYNTH_PARAM_FM_ALGORITHM) wrap = 8;
    if (wrap > 0) next = (next % wrap + wrap) % wrap;
    else {
        int32_t minimum = 0, maximum = 0;
        if (synthParameterRange(item.parameter, minimum, maximum))
            next = constrain(next, minimum, maximum);
    }
    if (!synthSetParameter(track, item.parameter, next)) return;
    if (item.parameter == SYNTH_PARAM_ENGINE) {
        // displayEngine() reflects the just-requested engine even though the
        // audio task applies the switch at its next block boundary (P2-8).
        g_soundBank = synthFirstSoundBank(track.displayEngine());
        g_soundParam = 0;
    }
}

static void adjustDrumParam(uint8_t lane, uint8_t row, int dir, bool fine) {
    DrumLane& d = g_drumLanes[lane];
    switch (row) {
        case 0: g_curDrumLane = (uint8_t)((lane + dir + NUM_DRUM_LANES) % NUM_DRUM_LANES); break;
        case 1: d.engine = (DrumEngine)(((int)d.engine + dir + ENG_COUNT) % ENG_COUNT); break;
        case 2:
            if (d.engine == ENG_SMPL) {
                if (g_numSamples)
                    d.sampleSlot = (int8_t)((((d.sampleSlot < 0 ? 0 : d.sampleSlot)) + dir
                                             + g_numSamples) % g_numSamples);
            } else {
                d.type = (uint8_t)(((int)d.type + dir + DT_COUNT) % DT_COUNT);
            }
            break;
        case 3: d.volume = constrain(d.volume + dir * (fine ? 0.01f : 0.05f), 0.0f, 1.0f); break;
        case 4: d.tune   = constrain(d.tune   + dir * (fine ? 0.1f  : 1.0f), -12.0f, 12.0f); break;
        case 5: d.decay  = constrain(d.decay  + dir * (fine ? 0.02f : 0.1f), 0.4f, 2.5f); break;
        case 6: d.chokeGroup = (uint8_t)(((int)d.chokeGroup + dir + 4) % 4); break;
    }
}

// ---------- arrows, per page ----------
static void arrow(uint8_t act, const KeySnap& now) {
    int dx = (act == ACT_LEFT) ? -1 : (act == ACT_RIGHT) ? 1 : 0;
    int dy = (act == ACT_UP)   ? -1 : (act == ACT_DOWN)  ? 1 : 0;

    switch (g_curPage) {
        case PAGE_PATTERN:
            if (dy) {
                if (g_curTrack < NUM_SYNTHS) {
                    // synth rows: down eventually enters drums (top = lane 8, TR order)
                    if (dy > 0) {
                        if (g_curTrack + 1 < NUM_SYNTHS) g_curTrack++;
                        else { g_curTrack = NUM_SYNTHS; g_curDrumLane = NUM_DRUM_LANES - 1; }
                    } else if (g_curTrack > 0) g_curTrack--;
                } else {
                    // drum grid is drawn kick-at-bottom: visual down = lane-1
                    if (dy > 0) { if (g_curDrumLane > 0) g_curDrumLane--; }
                    else {
                        if (g_curDrumLane < NUM_DRUM_LANES - 1) g_curDrumLane++;
                        else g_curTrack = NUM_SYNTHS - 1;      // exit up into synth 3
                    }
                }
            }
            if (dx) g_curStep = (uint8_t)((g_curStep + NUM_STEPS + dx) % NUM_STEPS);
            break;

        case PAGE_SOUND: {
            // P3: an engine changed via serial/project-load may have left
            // g_soundBank pointing at the previous engine's banks.
            if (g_curTrack < NUM_SYNTHS)
                g_soundBank = synthEnsureSoundBank(
                    g_synths[g_curTrack].displayEngine(), g_soundBank);
            uint8_t rows = (g_curTrack == NUM_SYNTHS) ? DRUM_PARAMS :
                synthSoundBankRows(g_soundBank);
            if (dy) g_soundParam = (uint8_t)((g_soundParam + rows + dy) % rows);
            if (dx) {
                bool fine = accentHeld(now);
                if (g_curTrack == NUM_SYNTHS) adjustDrumParam(g_curDrumLane, g_soundParam, dx, fine);
                else adjustSynthParam(g_synths[g_curTrack], g_soundParam, dx, fine);
            }
            break;
        }
        case PAGE_FX:
            if (dy) g_masterEffectParameter = static_cast<uint8_t>((g_masterEffectParameter + 6 + dy) % 6);
            if (dx) {
                const MasterEffectType effect = static_cast<MasterEffectType>(g_masterEffectSelection);
                const MasterEffectsSettings settings = g_masterEffects.settings();
                if (g_masterEffectParameter == 0) g_masterEffectSelection = static_cast<uint8_t>(
                    (g_masterEffectSelection + MASTER_EFFECT_COUNT + dx) % MASTER_EFFECT_COUNT);
                else if (g_masterEffectParameter == 1) g_masterEffects.setEnabled(effect, !g_masterEffects.enabled(effect));
                else if (g_masterEffectParameter == 2) g_masterEffects.setMix(effect, static_cast<uint8_t>(constrain(
                    static_cast<int>(settings.mix[effect]) + dx * 5, 0, 127)));
                else if (g_masterEffectParameter == 3) g_masterEffects.setFeedback(static_cast<uint8_t>(constrain(
                    static_cast<int>(settings.feedback) + dx * 5, 0, 120)));
                else if (g_masterEffectParameter == 4) g_masterEffects.setRate(static_cast<uint8_t>(constrain(
                    static_cast<int>(settings.rate) + dx * 4, 1, 127)));
                else g_masterEffects.setFilter(static_cast<uint8_t>(constrain(
                    static_cast<int>(settings.filter) + dx * 4, 1, 127)));
            }
            break;
        case PAGE_VOCODER:
            if (dy) g_vocoderParameter = static_cast<uint8_t>((g_vocoderParameter + 8 + dy) % 8);
            if (dx) {
                VocoderSettings v = g_vocoder.settings();
                if (g_vocoderParameter == 0) v.enabled = !v.enabled;
                else if (g_vocoderParameter == 1) v.source = static_cast<uint8_t>((v.source + 3 + dx) % 3);
                else if (g_vocoderParameter == 2) v.formantShift = static_cast<int8_t>(constrain(
                    static_cast<int>(v.formantShift) + dx, -12, 12));
                else {
                    uint8_t *parameter = g_vocoderParameter == 3 ? &v.resonance :
                        g_vocoderParameter == 4 ? &v.attack : g_vocoderParameter == 5 ? &v.release :
                        g_vocoderParameter == 6 ? &v.noise : &v.gate;
                    *parameter = static_cast<uint8_t>(constrain(static_cast<int>(*parameter) + dx * 5, 0, 127));
                }
                g_vocoder.applySettings(v);
            }
            break;
        case PAGE_CHORD:
            if (dy) g_chordParameter = static_cast<uint8_t>((g_chordParameter + 8 + dy) % 8);
            if (dx) {
                switch (g_chordParameter) {
                    case 0: {
                        const HiChordMode previous = g_hiChordPerformance.mode();
                        const HiChordMode next = static_cast<HiChordMode>(
                            (previous + HICHORD_MODE_COUNT + dx) % HICHORD_MODE_COUNT);
                        g_hiChordPerformance.setMode(next);
                        if (previous != next) {
                            if (previous == HICHORD_CHORD_HIRO) s_hiroStartUs = 0;
                            if (previous == HICHORD_EAR_TRAINER) stopEarAudition();
                            bool deferredDroneStop = false;
                            if (previous == HICHORD_SEQUENCER)
                                armHiChordBounce(HICHORD_BOUNCE_SEQUENCE);
                            else if (previous == HICHORD_DRUM_LOOPS)
                                armHiChordBounce(HICHORD_BOUNCE_DRUM_LOOP);
                            else if (previous == HICHORD_DRONE)
                                deferredDroneStop = armHiChordBounce(HICHORD_BOUNCE_DRONE);
                            if (!deferredDroneStop) inputStopHiChordPerformanceNotes();
                            if (next == HICHORD_EAR_TRAINER) startEarRound();
                        }
                        break;
                    }
                    case 1: g_chordSettings.key = static_cast<uint8_t>((g_chordSettings.key + 12 + dx) % 12); break;
                    case 2: g_chordSettings.scale = static_cast<ChordScale>(
                        (g_chordSettings.scale + CHORD_SCALE_COUNT + dx) % CHORD_SCALE_COUNT); break;
                    case 3: g_chordSettings.map = static_cast<ChordMap>((g_chordSettings.map + 3 + dx) % 3); break;
                    case 4: g_chordSettings.octave = static_cast<int8_t>(constrain(
                        static_cast<int>(g_chordSettings.octave) + dx, 2, 6)); break;
                    case 5: g_chordSettings.bassMode = static_cast<ChordBassMode>(
                        (g_chordSettings.bassMode + 3 + dx) % 3); break;
                    case 6: g_chordSettings.voiceLeading = !g_chordSettings.voiceLeading; break;
                    case 7:
                        if (g_hiChordPerformance.mode() == HICHORD_DRUM)
                            applyHiChordDrumKit(static_cast<uint8_t>((g_hiChordDrumKit + 7 + dx) % 7));
                        else if (g_hiChordPerformance.mode() == HICHORD_DRUM_LOOPS ||
                                 g_hiChordPerformance.mode() == HICHORD_AUTO_DRUM) {
                            g_hiChordGrooveVariation = static_cast<uint8_t>((g_hiChordGrooveVariation + 8 + dx) % 8);
                            writeHiChordGroove(g_hiChordGrooveStyle, g_hiChordGrooveVariation);
                        } else if (g_hiChordPerformance.mode() == HICHORD_CHORD_HIRO) {
                            g_hiChordPracticeSong = static_cast<uint8_t>((g_hiChordPracticeSong +
                                hiChordPracticeSongCount() + dx) % hiChordPracticeSongCount());
                            g_hiChordPracticePosition = 0;
                            s_hiroStartUs = 0;
                        } else if (g_hiChordPerformance.mode() == HICHORD_EAR_TRAINER) {
                            g_hiChordEarLevel = static_cast<uint8_t>((g_hiChordEarLevel + 4 + dx) % 4);
                            startEarRound();
                        } else if (g_hiChordPerformance.mode() == HICHORD_ARPEGGIO) {
                            if (accentHeld(now))
                                g_hiChordArpLayer = static_cast<uint8_t>((g_hiChordArpLayer + 3 + dx) % 3);
                            else
                                g_hiChordArpPattern = static_cast<uint8_t>((g_hiChordArpPattern + ARP_PATTERN_COUNT + dx) % ARP_PATTERN_COUNT);
                        } else if (g_hiChordPerformance.mode() == HICHORD_REPEAT) {
                            g_hiChordRepeatRate = static_cast<uint8_t>(
                                (g_hiChordRepeatRate + HICHORD_RATE_COUNT + dx) % HICHORD_RATE_COUNT);
                        } else if (g_hiChordPerformance.mode() == HICHORD_STRUM) {
                            g_hiChordStrumSpeed = static_cast<uint8_t>(
                                (g_hiChordStrumSpeed + HICHORD_STRUM_SPEED_COUNT + dx) %
                                HICHORD_STRUM_SPEED_COUNT);
                        } else if (g_hiChordPerformance.mode() == HICHORD_SEQUENCER) {
                            const int lengthIndex = g_hiChordSequenceLength / 4 - 1;
                            g_hiChordSequenceLength = static_cast<uint8_t>(
                                ((lengthIndex + 4 + dx) % 4 + 1) * 4);
                            if (g_curStep >= g_hiChordSequenceLength) g_curStep = 0;
                        } else g_chordSettings.inversion[g_chordDegree] = static_cast<int8_t>(constrain(
                            static_cast<int>(g_chordSettings.inversion[g_chordDegree]) + dx, -2, 3));
                        break;
                }
            }
            break;
        case PAGE_KO:
            if (dy || dx) g_poEffectSelection = static_cast<uint8_t>(
                (g_poEffectSelection + PO_FX_COUNT + (dy ? dy : dx)) % PO_FX_COUNT);
            break;
        case PAGE_MEDO:
            if (dy) g_medoParameter = static_cast<uint8_t>((g_medoParameter + 7 + dy) % 7);
            if (dx) {
                const MedoRole role = g_medoPerformance.role();
                if (g_medoParameter == 0) g_medoPerformance.setRole(static_cast<MedoRole>(
                    (role + MEDO_ROLE_COUNT + dx) % MEDO_ROLE_COUNT));
                else if (g_medoParameter == 1) g_medoPerformance.setQuantize(role,
                    static_cast<MedoQuantize>((g_medoPerformance.settings(role).quantize + 3 + dx) % 3));
                else if (g_medoParameter == 2) g_medoPerformance.setVolume(role,
                    static_cast<uint8_t>(constrain(static_cast<int>(g_medoPerformance.settings(role).volume) + dx * 5, 0, 127)));
                else if (g_medoParameter == 3) g_medoPerformance.setOctave(role, static_cast<int8_t>(constrain(
                    static_cast<int>(g_medoPerformance.settings(role).octave) + dx, -4, 4)));
                else if (g_medoParameter == 4) {
                    const uint16_t bars = static_cast<uint16_t>(constrain(
                        static_cast<int>(g_medoPerformance.sharedBars()) + dx, 1, 128));
                    if (g_medoPerformance.setSharedBars(bars)) g_eventLooper.setAllBars(bars);
                } else if (g_medoParameter == 5)
                    g_medoPerformance.setScale(static_cast<MedoScale>(
                        (g_medoPerformance.scale() + MEDO_SCALE_COUNT + dx) % MEDO_SCALE_COUNT));
                else if (accentHeld(now)) {
                    static const uint8_t rates[] = {1,2,4,8};
                    uint8_t index = 0; while (index < 3 && rates[index] != g_medoPerformance.arpRate()) ++index;
                    g_medoPerformance.setArpRate(rates[(index + 4 + dx) % 4]);
                } else g_medoPerformance.setArpDirection(static_cast<MedoArpDirection>(
                    (g_medoPerformance.arpDirection() + MEDO_ARP_COUNT + dx) % MEDO_ARP_COUNT));
            }
            if (dx || dy) outputMedoGesture(MEDO_SLIDE,
                static_cast<uint8_t>(constrain(64 + dx * 24 - dy * 24, 0, 127)));
            break;
        case PAGE_SAMPLE:
            if (g_sampleEditMode == 0) {
                if (dy < 0 && g_fileSel > 0) g_fileSel--;
                if (dy > 0 && g_fileSel + 1 < g_fileCount) g_fileSel++;
                if (dx) g_streamSampleSlot = static_cast<uint8_t>(
                    (g_streamSampleSlot + SAMPLER_SLOT_COUNT + dx) % SAMPLER_SLOT_COUNT);
            } else {
                if (dy) g_sampleParam = static_cast<uint8_t>((g_sampleParam + 6 + dy) % 6);
                if (dx && accentHeld(now))
                    g_curStep = static_cast<uint8_t>((g_curStep + NUM_STEPS + dx) % NUM_STEPS);
                else if (dx) adjustSampleParameter(dx);
            }
            break;

        case PAGE_LOOPS:
            if (dy) g_loopCursor = static_cast<uint8_t>(
                (g_loopCursor + LOOP_STREAM_TRACKS + dy) % LOOP_STREAM_TRACKS);
            if (dx && accentHeld(now)) {
                g_hiChordLoopBars = static_cast<uint8_t>(
                    (g_hiChordLoopBars + 9 + dx) % 9);
                uiStatus(g_hiChordLoopBars ? "TRACK 1 FIXED BARS" : "TRACK 1 FREE LENGTH");
            } else if (dx) {
                const LoopStreamTrackSnapshot& item =
                    loopEngineSnapshot().tracks[g_loopCursor];
                const int volume = constrain(
                    static_cast<int>(item.volumeQ15) * 100 / 32767 + dx * 5, 0, 100);
                loopEngineSetVolume(g_loopCursor, static_cast<uint8_t>(volume));
            }
            break;

        case PAGE_EVENT:
            if (dy) g_eventCursor = static_cast<uint8_t>(
                (g_eventCursor + EVENT_LOOP_TRACKS + dy) % EVENT_LOOP_TRACKS);
            if (dx) {
                const int bars = constrain(
                    static_cast<int>(g_eventLooper.bars(g_eventCursor)) + dx,
                    1, static_cast<int>(EVENT_LOOP_MAX_BARS));
                g_eventLooper.setBars(g_eventCursor, static_cast<uint16_t>(bars));
            }
            break;

        case PAGE_MOTION:
            if (dy) g_motionCursor = static_cast<uint8_t>(
                (g_motionCursor + MOTION_MAPPING_COUNT + dy) % MOTION_MAPPING_COUNT);
            if (dx) {
                const MotionSnapshot motion = motionSnapshot();
                int source = motion.mappings[g_motionCursor].source;
                // P2-3 (reconciliation report): sources occupy 1..COUNT-1
                // (0 = NONE), so cycle in 0-based space and shift back.
                // The old expression skipped a source going right and made
                // left a no-op (proven by execution).
                source = ((source - 1) + dx + (MOTION_SOURCE_COUNT - 1)) %
                         (MOTION_SOURCE_COUNT - 1) + 1;
                uint8_t target = motion.mappings[g_motionCursor].target;
                if (target == MOTION_TARGET_NONE) target = MOTION_TARGET_SYNTH1_CUTOFF;
                motionSetMapping(g_motionCursor, static_cast<MotionSource>(source),
                                 static_cast<MotionTarget>(target));
            }
            break;

        case PAGE_SONG:
            if (dy) g_songCursor = (uint8_t)((g_songCursor + SONG_LENGTH + dy * 16) % SONG_LENGTH);
            if (dx) g_songCursor = (uint8_t)((g_songCursor + SONG_LENGTH + dx) % SONG_LENGTH);
            break;
        default: break;
    }
    g_needRedraw = true;
}

// ---------- immediate actions (key-down) ----------
static void doImmediate(uint8_t act, const KeySnap& now) {
    switch (act) {
        case ACT_LEFT: case ACT_RIGHT: case ACT_UP: case ACT_DOWN:
            arrow(act, now); break;

        case ACT_SLIDE:
            if (g_curPage == PAGE_LOOPS) {
                const LoopEngineSnapshot loops = loopEngineSnapshot();
                loopEngineSetMetronome(!loops.metronome);
                uiStatus(loops.metronome ? "METRONOME OFF" : "METRONOME ON");
            } else if (g_curPage == PAGE_SAMPLE) {
                g_samplerSequence.clearEvent(g_curPattern, g_curStep, g_streamSampleSlot);
                uiStatus("SAMPLE STEP CLEARED");
                g_needRedraw = true;
            } else if (g_curTrack < NUM_SYNTHS) {
                SynthCell& c = g_patterns[g_curPattern].synth[g_curTrack][g_curStep];
                if (!c.empty()) {
                    c.slide = !c.slide;
                    if (c.slide && g_synths[g_curTrack].voices > 1)
                        uiStatus("SLIDE = MONO ONLY");
                    g_needRedraw = true;
                }
            }
            break;

        case ACT_REC:
            if (g_curPage == PAGE_DIAG) {
                uiStatus(sdDiagnosticsStart() ? "SD TEST STARTED" : "SD TEST BUSY");
            } else if (g_curPage == PAGE_SAMPLE) {
                if (g_sampleEditMode == 2) {
                    uiStatus(g_samplerSequence.removeLock(
                                 g_curPattern, g_curStep, g_streamSampleSlot)
                             ? "LOCK CLEARED" : "NO LOCK");
                } else if (g_sampleEditMode == 0 && g_fileCount && streamingSamplerAssign(
                        g_streamSampleSlot, g_fileList[g_fileSel],
                        static_cast<SamplerSlotMode>(g_streamSampleMode)))
                    uiStatus("STREAM SLOT QUEUED");
                else if (g_sampleEditMode == 0) uiStatus("ASSIGN FAILED");
                else uiStatus("EDIT WITH ARROWS");
            } else if (g_curPage == PAGE_LOOPS) {
                const LoopEngineSnapshot loops = loopEngineSnapshot();
                if (loops.recordTrack == g_loopCursor)
                    uiStatus(loopEngineStopRecording(g_loopCursor) ? "FINALIZING" : "STOP FAILED");
                else
                    uiStatus(requestLoopRecord(g_loopCursor)
                             ? (g_loopCursor == 0 && g_hiChordLoopBars
                                    ? "4-BEAT COUNT-IN" : "LOOP ARMED")
                             : "RECORD FAILED");
            } else if (g_curPage == PAGE_EVENT) {
                const bool armed = !g_eventLooper.track(g_eventCursor).armed;
                g_eventLooper.setArmed(g_eventCursor, armed);
                uiStatus(armed ? "EVENT REC ARMED" : "EVENT REC OFF");
            } else if (g_curPage == PAGE_KO) {
                const PoEffect effect = static_cast<PoEffect>(g_poEffectSelection);
                g_poEffectProcessor.engage(effect);
                if (g_recEnabled || g_playing)
                    g_poPatternEffects.set(g_curPattern, g_curStep, effect);
                uiStatus("PUNCH FX ON");
            } else if (g_curPage == PAGE_FX) {
                const MasterEffectType effect = static_cast<MasterEffectType>(g_masterEffectSelection);
                g_masterEffects.setEnabled(effect, !g_masterEffects.enabled(effect));
                uiStatus(g_masterEffects.enabled(effect) ? "EFFECT ON" : "EFFECT OFF");
            } else if (g_curPage == PAGE_VOCODER) {
                VocoderSettings v = g_vocoder.settings(); v.enabled = !v.enabled;
                g_vocoder.applySettings(v); uiStatus(v.enabled ? "VOCODER ON" : "VOCODER OFF");
            } else if (g_curPage == PAGE_MEDO) {
                const uint8_t role = g_medoPerformance.role();
                const bool armed = !g_eventLooper.track(role).armed;
                g_eventLooper.setArmed(role, armed);
                uiStatus(armed ? "ROLE REC ARMED" : "ROLE REC OFF");
            } else {
                g_recEnabled = !g_recEnabled;
                uiStatus(g_recEnabled ? "REC ON" : "REC OFF");
            }
            g_needRedraw = true; break;

        case ACT_BANK:
            if (g_curPage == PAGE_SAMPLE) {
                g_sampleEditMode = static_cast<uint8_t>((g_sampleEditMode + 1u) % 3u);
                uiStatus(g_sampleEditMode == 0 ? "SAMPLE BROWSER" :
                         g_sampleEditMode == 1 ? "SLOT SOUND" : "STEP LOCK");
            } else if (g_curPage == PAGE_SOUND && g_curTrack < NUM_SYNTHS) {
                g_soundBank = synthNextSoundBank(
                    g_synths[g_curTrack].displayEngine(), g_soundBank);
                g_soundParam = 0;
                uiStatus(synthSoundBankName(g_soundBank));
            } else if (g_curPage == PAGE_CHORD) {
                if (g_hiChordPerformance.mode() == HICHORD_ARPEGGIO) {
                    if (accentHeld(now)) {
                        g_hiChordArpLayer = static_cast<uint8_t>((g_hiChordArpLayer + 1u) % 3u);
                        uiStatus("ARP LAYER");
                    } else {
                        g_hiChordArpRate = static_cast<uint8_t>(
                            (g_hiChordArpRate + 1u) % HICHORD_RATE_COUNT);
                        uiStatus("ARP RATE");
                    }
                } else if (g_hiChordPerformance.mode() == HICHORD_REPEAT) {
                    g_hiChordRepeatRate = static_cast<uint8_t>(
                        (g_hiChordRepeatRate + 1u) % HICHORD_RATE_COUNT);
                    uiStatus("REPEAT RATE");
                } else if (g_hiChordPerformance.mode() == HICHORD_STRUM) {
                    g_hiChordStrumSpeed = static_cast<uint8_t>(
                        (g_hiChordStrumSpeed + 1u) % HICHORD_STRUM_SPEED_COUNT);
                    uiStatus("STRUM SPEED");
                } else {
                    g_chordSettings.map = static_cast<ChordMap>((g_chordSettings.map + 1) % 3);
                    uiStatus("CHORD MAP");
                }
            } else if (g_curPage == PAGE_FX) {
                g_masterEffectSelection = static_cast<uint8_t>((g_masterEffectSelection + 1) % MASTER_EFFECT_COUNT);
                uiStatus("NEXT EFFECT");
            } else if (g_curPage == PAGE_LOOPS) {
                const LoopEngineSnapshot loops = loopEngineSnapshot();
                const bool solo = loops.soloTrack != g_loopCursor;
                uiStatus(loopEngineSetSolo(g_loopCursor, solo)
                         ? (solo ? "LOOP SOLO" : "SOLO OFF") : "SOLO FAILED");
            } else if (g_curPage == PAGE_MEDO) {
                g_medoPerformance.setArpEnabled(!g_medoPerformance.arpEnabled());
                uiStatus(g_medoPerformance.arpEnabled() ? "MEDO ARP ON" : "MEDO ARP OFF");
            } else {
                g_patternBank ^= 1;
                uiStatus(g_patternBank ? "PATTERNS 9-16" : "PATTERNS 1-8");
            }
            g_needRedraw = true; break;

        case ACT_ACCENT: break;   // pure modifier
        default: break;
    }
}

// ---------- short actions (release < 450ms) ----------
static void doShort(uint8_t act) {
    if (g_curPage == PAGE_SAMPLE && act == ACT_LOAD) {
        s_sampleCopySlot = g_streamSampleSlot;
        s_sampleCopySlice = g_samplerSlotBank.slot(g_streamSampleSlot).mode == SAMPLER_SLOT_SLICED
            ? s_selectedSampleSlice : 0xFF;
        uiStatus(s_sampleCopySlice == 0xFF ? "SOUND COPIED" : "SLICE COPIED");
        return;
    }
    if (g_curPage == PAGE_SAMPLE && act == ACT_SAVE) {
        bool copied = false;
        if (s_sampleCopySlot < SAMPLER_SLOT_COUNT && s_sampleCopySlice != 0xFF &&
            g_samplerSlotBank.slot(g_streamSampleSlot).mode == SAMPLER_SLOT_SLICED)
            copied = s_sampleCopySlot == g_streamSampleSlot
                ? g_samplerSlotBank.copySlice(s_sampleCopySlot, s_sampleCopySlice,
                                              g_streamSampleSlot, s_selectedSampleSlice)
                : streamingSamplerCopySlice(s_sampleCopySlot, s_sampleCopySlice,
                                            g_streamSampleSlot, s_selectedSampleSlice);
        else if (s_sampleCopySlot < SAMPLER_SLOT_COUNT)
            copied = g_samplerSlotBank.copySlot(s_sampleCopySlot, g_streamSampleSlot);
        uiStatus(copied ? (s_sampleCopySlice == 0xFF ? "PASTED" : "SLICE COPY QUEUED")
                        : "PASTE FAILED");
        return;
    }
    // track select
    if (act >= ACT_TRACK1 && act <= ACT_TRACKD) {
        g_curTrack = (uint8_t)(act - ACT_TRACK1);          // 0..2, 3 = drums
        if (g_curPage == PAGE_SOUND && g_curTrack < NUM_SYNTHS) {
            g_soundBank = synthFirstSoundBank(g_synths[g_curTrack].displayEngine());
            g_soundParam = 0;
        }
        g_needRedraw = true; return;
    }
    // pattern keys: context
    if (act >= ACT_PAT1 && act <= ACT_PAT8) {
        uint8_t keyIndex = (uint8_t)(act - ACT_PAT1);
        if (g_curPage == PAGE_LOOPS) {
            if (keyIndex < LOOP_STREAM_TRACKS) g_loopCursor = keyIndex;
            g_needRedraw = true;
        } else if (g_curPage == PAGE_EVENT) {
            if (keyIndex < EVENT_LOOP_TRACKS) g_eventCursor = keyIndex;
            g_needRedraw = true;
        } else if (g_curPage == PAGE_KO) {
            g_poEffectSelection = static_cast<uint8_t>(g_patternBank * 8 + keyIndex);
            g_poEffectProcessor.engage(static_cast<PoEffect>(g_poEffectSelection));
            if (g_recEnabled || g_playing)
                g_poPatternEffects.set(g_curPattern, g_curStep,
                    static_cast<PoEffect>(g_poEffectSelection));
            g_needRedraw = true;
        } else if (g_curPage == PAGE_CHORD) {
            if (keyIndex < 4)
                uiStatus(performanceRecallPreset(keyIndex) ? "PRESET LOADED" : "PRESET EMPTY");
            g_needRedraw = true;
        } else if (g_curPage == PAGE_SAMPLE) {
            if (g_fileCount) {
                int slot = samplerLoad(g_fileList[g_fileSel]);
                if (slot >= 0) {
                    DrumLane& d = g_drumLanes[keyIndex];
                    d.engine = ENG_SMPL; d.sampleSlot = (int8_t)slot; d.smp.init();
                    uiStatus("ASSIGNED");
                } else uiStatus("LOAD FAILED");
                g_needRedraw = true;
            }
        } else if (g_curPage == PAGE_SONG) {
            uint8_t k = (uint8_t)(g_patternBank * 8 + keyIndex);
            g_song[g_songCursor] = k;
            g_songCursor = (g_songCursor + 1) % SONG_LENGTH;
            g_needRedraw = true;
        } else {
            uint8_t k = (uint8_t)(g_patternBank * 8 + keyIndex);
            g_curPattern = k;
            if (!g_playing && !g_songMode) g_playPattern = k;
            uiStatus("PATTERN"); g_needRedraw = true;
        }
        return;
    }

    switch (act) {
        case ACT_LOAD: {
            char m[24];
            snprintf(m, sizeof(m), "P%u%s hold=LOAD", g_curProject + 1,
                     storageProjectExists(g_curProject) ? "*" : " empty");
            uiStatus(m); break;
        }
        case ACT_SAVE: {
            char m[24];
            snprintf(m, sizeof(m), "P%u hold=SAVE", g_curProject + 1);
            uiStatus(m); break;
        }
        case ACT_BPM_DN:
            g_bpm = (uint16_t)constrain((int)g_bpm - 1, 40, 300);
            g_needRedraw = true; break;
        case ACT_BPM_UP:
            g_bpm = (uint16_t)constrain((int)g_bpm + 1, 40, 300);
            g_needRedraw = true; break;

        case ACT_PAGE:
            g_curPage = (Page)(((int)g_curPage + 1) % PAGE_COUNT);
            if (g_curPage == PAGE_SAMPLE) uiScanSampleDir();
            g_needRedraw = true; break;

        case ACT_PLAY:
            if (g_curPage == PAGE_CHORD &&
                g_hiChordPerformance.mode() == HICHORD_CHORD_HIRO)
                startChordHiro();
            else if (g_playing) sequencerStop(); else sequencerStart(false);
            g_needRedraw = true; break;

        case ACT_CLR:
            if (g_curPage == PAGE_SAMPLE) {
                uiStatus(streamingSamplerClear(g_streamSampleSlot)
                         ? "SLOT CLEAR QUEUED" : "CLEAR FAILED");
            } else if (g_curPage == PAGE_LOOPS) {
                uiStatus(loopEngineClear(g_loopCursor) ? "LOOP CLEAR QUEUED" : "CLEAR FAILED");
            } else if (g_curPage == PAGE_EVENT) {
                g_eventLooper.clearTrack(g_eventCursor); uiStatus("EVENTS CLEARED");
            } else if (g_curPage == PAGE_MOTION) {
                motionClearMapping(g_motionCursor); uiStatus("MAPPING CLEARED");
            } else if (g_curPage == PAGE_KO) {
                g_poPatternEffects.set(g_curPattern, g_curStep, PO_FX_NONE);
                g_poEffectProcessor.engage(PO_FX_NONE); uiStatus("FX CLEARED");
            } else if (g_curPage == PAGE_MEDO) {
                g_eventLooper.clearTrack(g_medoPerformance.role()); uiStatus("ROLE CLEARED");
            } else if (g_curPage == PAGE_SONG) g_song[g_songCursor] = SONG_EMPTY;
            else clearCellAtCursor();
            g_needRedraw = true; break;

        case ACT_SONG:
            if (g_curPage == PAGE_SAMPLE) {
                g_streamSampleMode = g_streamSampleMode == SAMPLER_SLOT_MELODIC
                    ? SAMPLER_SLOT_SLICED : SAMPLER_SLOT_MELODIC;
                g_samplerSlotBank.setMode(
                    g_streamSampleSlot, static_cast<SamplerSlotMode>(g_streamSampleMode));
                uiStatus(g_streamSampleMode == SAMPLER_SLOT_SLICED ? "SLICE MODE" : "MELODIC MODE");
            } else if (g_curPage == PAGE_KO) {
                static const uint8_t swings[] = {50, 58, 66, 75};
                uint8_t next = 0;
                while (next < 4 && swings[next] <= g_swing) ++next;
                g_swing = swings[next % 4]; uiStatus("SWING");
            } else if (g_curPage == PAGE_LOOPS) {
                const LoopEngineSnapshot loops = loopEngineSnapshot();
                if (loopEngineSetPaused(!loops.paused))
                    uiStatus(loops.paused ? "LOOPS RESUME" : "LOOPS PAUSE");
                else uiStatus("STOP RECORDING FIRST");
            } else {
                g_songMode = !g_songMode;
                uiStatus(g_songMode ? "SONG MODE" : "PATTERN MODE");
            }
            g_needRedraw = true; break;

        case ACT_AUX:
            if (g_curPage == PAGE_SAMPLE && g_fileCount) {
                if (!streamingSamplerTrigger(g_streamSampleSlot, 0)) {
                    int slot = samplerLoad(g_fileList[g_fileSel]);
                    if (slot >= 0) g_previewVoice.trigger(slot, 1.0f, 0.9f);
                    else uiStatus("LOAD FAILED");
                }
                g_needRedraw = true;
            } else if (g_curPage == PAGE_LOOPS) {
                const LoopStreamState state = loopEngineSnapshot().tracks[g_loopCursor].state;
                const bool muted = state != LOOP_STREAM_MUTED;
                uiStatus(loopEngineSetMuted(g_loopCursor, muted)
                         ? (muted ? "LOOP MUTED" : "LOOP PLAYING") : "MUTE FAILED");
            } else if (g_curPage == PAGE_EVENT) {
                const bool muted = !g_eventLooper.track(g_eventCursor).muted;
                g_eventLooper.setMuted(g_eventCursor, muted);
                uiStatus(muted ? "EVENT MUTED" : "EVENT PLAYING");
            } else if (g_curPage == PAGE_MOTION) {
                const MotionSnapshot motion = motionSnapshot();
                uint8_t source = motion.mappings[g_motionCursor].source;
                if (source == MOTION_SOURCE_NONE) source = MOTION_SOURCE_TILT_X;
                int target = motion.mappings[g_motionCursor].target;
                target = target % (MOTION_TARGET_COUNT - 1) + 1;
                motionSetMapping(g_motionCursor, static_cast<MotionSource>(source),
                                 static_cast<MotionTarget>(target));
                uiStatus("TARGET CHANGED");
            } else if (g_curPage == PAGE_KO) {
                g_poEffectProcessor.engage(PO_FX_NONE); uiStatus("FX OFF");
            } else if (g_curPage == PAGE_CHORD &&
                       g_hiChordPerformance.mode() == HICHORD_EAR_TRAINER) {
                queueEarAudition(true); uiStatus("ROOT HINT");
            } else if (g_curPage == PAGE_SONG) {
                g_songLoopStart = g_songCursor;
                uiStatus("LOOP SET"); g_needRedraw = true;
            } else {
                uiStatus("hold = MIC SAMPLE");
            }
            break;
        default: break;
    }
}

// ---------- long actions (held 450ms) ----------
static void doLong(uint8_t act) {
    if (act >= ACT_TRACK1 && act <= ACT_TRACK3) {
        uint8_t t = (uint8_t)(act - ACT_TRACK1);
        g_synthMute[t] = !g_synthMute[t];
        uiStatus(g_synthMute[t] ? "MUTED" : "UNMUTED");
        g_needRedraw = true; return;
    }
    if (act == ACT_TRACKD) {
        g_drumMute = !g_drumMute;
        uiStatus(g_drumMute ? "DRUMS MUTED" : "DRUMS ON");
        g_needRedraw = true; return;
    }
    if (act >= ACT_PAT1 && act <= ACT_PAT8) {
        if (g_curPage == PAGE_CHORD) {
            const uint8_t preset = static_cast<uint8_t>(act - ACT_PAT1);
            if (preset < 4) uiStatus(performanceStorePreset(preset) ? "PRESET STORED" : "PRESET FAILED");
            g_needRedraw = true; return;
        }
        if (g_curPage == PAGE_FX || g_curPage == PAGE_VOCODER ||
            g_curPage == PAGE_CHORD || g_curPage == PAGE_SAMPLE ||
            g_curPage == PAGE_KO || g_curPage == PAGE_LOOPS ||
            g_curPage == PAGE_EVENT || g_curPage == PAGE_MEDO || g_curPage == PAGE_MOTION ||
            g_curPage == PAGE_SONG) return;
        uint8_t k = (uint8_t)(g_patternBank * 8 + (act - ACT_PAT1));
        clonePatternTo(k);
        g_curPattern = k;
        if (!g_playing && !g_songMode) g_playPattern = k;
        char m[16]; snprintf(m, sizeof(m), "CLONED>%u", k + 1);
        uiStatus(m); g_needRedraw = true; return;
    }
    switch (act) {
        case ACT_LOAD:
            uiStatus(storageLoadProject(g_curProject) ? "LOADED" : "LOAD FAILED");
            g_needRedraw = true; break;
        case ACT_SAVE:
            uiStatus(storageSaveProject(g_curProject) ? "SAVED" : "SAVE FAILED");
            break;
        case ACT_PAGE:
            g_curPage = PAGE_PATTERN; g_needRedraw = true; break;
        case ACT_PLAY:
            sequencerStart(true); g_needRedraw = true; break;
        case ACT_CLR:
            if (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SOUND) {
                memset(&g_patterns[g_curPattern], 0, sizeof(Pattern));
                uiStatus("PATTERN CLEARED"); g_needRedraw = true;
            }
            break;
        case ACT_BPM_DN:
            if (g_curPage == PAGE_SONG) {
                if (g_curProject > 0) g_curProject--;
                uiStatus("PROJECT");
            } else if (g_curOctave > 1) g_curOctave--;
            g_needRedraw = true; break;
        case ACT_BPM_UP:
            if (g_curPage == PAGE_SONG) {
                if (g_curProject < NUM_PROJECT_SLOTS - 1) g_curProject++;
                uiStatus("PROJECT");
            } else if (g_curOctave < 7) g_curOctave++;
            g_needRedraw = true; break;

        case ACT_SONG:
            if (g_curPage == PAGE_SONG) {
                if (stemRecorderIsBusy())
                    uiStatus(stemRecorderStop() ? "STEMS SAVING..." : "STEM STOP FAILED");
                else
                    uiStatus(stemRecorderStart() ? "STEMS RECORDING" : "STEMS BUSY");
            } else if (g_curPage == PAGE_SAMPLE) {
                const StreamingSamplerSnapshot snapshot = streamingSamplerSnapshot();
                if (snapshot.recordInput == STREAM_SAMPLE_INPUT_BUS &&
                    (snapshot.recordState == STREAM_SAMPLE_REC_STARTING ||
                     snapshot.recordState == STREAM_SAMPLE_REC_RECORDING)) {
                    uiStatus(streamingSamplerStopRecord() ? "SAMPLE SAVING..." : "STOP FAILED");
                } else {
                    uiStatus(streamingSamplerBeginRecord(
                                 g_streamSampleSlot,
                                 static_cast<SamplerSlotMode>(g_streamSampleMode),
                                 SAMPLE_RATE, STREAM_SAMPLE_INPUT_BUS)
                             ? "BUS SAMPLING..." : "SAMPLE BUSY");
                }
            } else if (g_playing) resampleArm();
            else uiStatus("PLAY, THEN HOLD");
            break;

        case ACT_AUX:
            if (g_curPage == PAGE_SONG) {
                if (masterRecorderIsBusy())
                    uiStatus(masterRecorderStop() ? "MASTER SAVING..." : "MASTER STOP FAILED");
                else
                    uiStatus(masterRecorderStart() ? "MASTER RECORDING" : "MASTER BUSY");
            } else if (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SOUND ||
                g_curPage == PAGE_SAMPLE || g_curPage == PAGE_CHORD) {
                const bool started = g_curPage == PAGE_SAMPLE
                    ? micStreamRecStart(g_streamSampleSlot,
                                        static_cast<SamplerSlotMode>(g_streamSampleMode))
                    : g_curPage == PAGE_CHORD && g_hiChordPerformance.mode() == HICHORD_TUNER
                        ? micTunerStart()
                    : g_curPage == PAGE_CHORD ? micHiChordRecStart(g_streamSampleSlot)
                    : micRecStart(g_curDrumLane);
                if (started) g_recPadKc = (uint8_t)'.';
                else uiStatus("MIC BUSY");
            }
            break;
        default: break;
    }
}

// ---------- piano / pads (key-down, latency-critical path) ----------
static void doPiano(uint8_t kc, const KeySnap& now) {
    if (g_curPage != PAGE_PATTERN && g_curPage != PAGE_SOUND) return;

    if (g_curTrack == NUM_SYNTHS) {
        int8_t lane = padLane(kc);
        if (lane < 0) return;
        g_curDrumLane = (uint8_t)lane;
        if (resamplePending()) { resampleCommit(lane); return; }
        if (g_curPage == PAGE_SOUND) triggerLaneLive(lane);
        else                         liveDrumHit(lane);
        g_needRedraw = true;
        return;
    }

    int8_t semi = pianoSemi(kc);
    if (semi < 0) return;
    uint8_t note, oct; semiToNote(semi, note, oct);
    bool accent = accentHeld(now);
    const uint8_t midiNote = static_cast<uint8_t>((oct + 1u) * 12u + note - 1u);

    if (g_curPage == PAGE_SOUND) {
        // P2-9: audition notes are live-held; mark them so a running
        // sequencer's per-step housekeeping cannot clip them.
        g_synths[g_curTrack].noteOnLive(noteToFreq(note, oct), accent, false,
                                        midiNote, accent ? 127 : 96);
        midiOutputNoteOn(g_curTrack, midiNote, accent ? 127 : 96);
        rememberMidiNote(kc, g_curTrack, midiNote, true);  // audition (P3)
        return;
    }
    bool legato = heldPianoCount(s_prev) >= 1;
    liveSynthNote(g_curTrack, note, oct, accent, legato);
    rememberMidiNote(kc, g_curTrack, midiNote);
}

static bool doMedoKey(uint8_t kc, const KeySnap &now) {
    const MedoRole role = g_medoPerformance.role();
    if (role == MEDO_DRUM) {
        const int8_t lane = padLane(kc);
        if (lane < 0) return false;
        outputMedoGesture(MEDO_CLICK, g_medoPerformance.settings(role).volume);
        eventLooperSetRecordRoleGain(true);
        liveDrumHit(static_cast<uint8_t>(lane),
                    g_medoPerformance.settings(role).volume);
        eventLooperSetRecordRoleGain(false); return true;
    }
    if (role == MEDO_SAMPLE) {
        const int8_t key = samplePerformanceKey(kc);
        if (key < 0) return false;
        outputMedoGesture(MEDO_CLICK, g_medoPerformance.settings(role).volume);
        eventLooperSetRecordRoleGain(true);
        liveSampleHit(g_streamSampleSlot, static_cast<uint8_t>(key),
                      g_medoPerformance.settings(role).volume);
        eventLooperSetRecordRoleGain(false); return true;
    }
    if (role == MEDO_CHORD && chordKeyDegree(kc) >= 0) {
        outputMedoGesture(MEDO_CLICK, g_medoPerformance.settings(role).volume);
        playChord(kc, now, g_medoPerformance.settings(role).volume,
                  g_medoPerformance.settings(role).octave,
                  g_medoPerformance.arpEnabled(), EVENT_ROLE_CHORD); return true;
    }
    const int8_t semi = pianoSemi(kc);
    if (semi < 0) return false;
    const int octaveShift = g_medoPerformance.settings(role).octave;
    const int midi = g_medoPerformance.quantizeNote(static_cast<uint8_t>(
        constrain((g_curOctave + 1 + octaveShift) * 12 + semi, 12, 127)));
    const uint8_t track = role == MEDO_BASS ? 0 : 1;
    outputMedoGesture(MEDO_CLICK, g_medoPerformance.settings(role).volume);
    if (accentHeld(now)) outputMedoGesture(MEDO_PRESS, 110);
    eventLooperSetRecordRoleGain(true);
    liveSynthNote(track, static_cast<uint8_t>(midi % 12 + 1),
                  static_cast<uint8_t>(midi / 12 - 1), accentHeld(now), false,
                  g_medoPerformance.settings(role).volume);
    eventLooperSetRecordRoleGain(false);
    rememberMidiNote(kc, track, static_cast<uint8_t>(midi));
    return true;
}

// ---------- main entry ----------
void inputInit() {
    s_prev = KeySnap();
    s_nHolds = 0; s_rptAct = ACT_NONE;
    s_heldMidiNoteCount = 0;
    s_heldChordCount = 0;
    s_chordReleaseCount = 0;
    s_autoDrumCount = 0;
    s_poEffectKey = KC_NONE;
    s_hiroStartUs = 0; s_hiroScore = 0;
    s_earAuditionStartUs = 0; s_earAuditionNext = s_earAuditionEnd = 0;
    s_earRandom = micros() | 1u;
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track)
        s_previousLoopState[track] = static_cast<uint8_t>(LOOP_STREAM_EMPTY);
    g_poLiveEffectActive = false;
    g_holdProg = 0; g_holdLabel[0] = 0;
}

void inputUpdate() {
    M5Cardputer.update();
    uint32_t nowMs = millis();
    updateHeldChords();
    updateChordHiro(micros());
    updateEarAudition(micros());
    updateLoopAutoAdvance();

    // -- CLR held while live-recording = erase at playhead --
    if (g_playing && g_recEnabled && s_prev.has('z')) clearStepAtPlayhead();

    // -- auto-repeat --
    if (s_rptAct != ACT_NONE && s_prev.has(s_rptKc) && nowMs >= s_rptNext) {
        s_rptCount++;
        doImmediate(s_rptAct, s_prev);
        s_rptNext = nowMs + RPT_RATE_MS;
    }

    // -- long-press firing + progress bar (runs even without key changes) --
    float prog = 0.0f; const char* lbl = "";
    for (uint8_t i = 0; i < s_nHolds; i++) {
        Hold& h = s_holds[i];
        uint32_t held = nowMs - h.t0;
        // CLR in erase-mode never fires short/long
        if (h.act == ACT_CLR && g_playing && g_recEnabled) { h.fired = true; continue; }
        if (!h.fired && held >= LONG_PRESS_MS) { h.fired = true; doLong(h.act); }
        if (!h.fired && held >= HINT_AFTER_MS) {
            float p = (float)(held - HINT_AFTER_MS) / (LONG_PRESS_MS - HINT_AFTER_MS);
            if (p > prog) { prog = p; lbl = actLongName(h.act); }
        }
    }
    // chord: LOAD + SAVE held simultaneously = DEMO
    {
        int iLoad = -1, iSave = -1;
        for (uint8_t i = 0; i < s_nHolds; i++) {
            if (s_holds[i].act == ACT_LOAD && !s_holds[i].fired) iLoad = i;
            if (s_holds[i].act == ACT_SAVE && !s_holds[i].fired) iSave = i;
        }
        if (iLoad >= 0 && iSave >= 0) {
            s_holds[iLoad].fired = true;
            s_holds[iSave].fired = true;
            loadDemoPattern();
            uiStatus("DEMO");
            g_needRedraw = true;
            prog = 0; lbl = "";
        }
    }

    // during mic capture the footer bar is the level meter (mic_sampler owns it)
    if (!micRecActive() && prog != g_holdProg) {
        static uint32_t lastAnim = 0;
        g_holdProg = prog;
        strncpy(g_holdLabel, lbl, sizeof(g_holdLabel) - 1);
        g_holdLabel[sizeof(g_holdLabel) - 1] = 0;
        if (nowMs - lastAnim > 40) { lastAnim = nowMs; g_needRedraw = true; }
    }

    if (!M5Cardputer.Keyboard.isChange()) return;

    // -- build snapshot --
    Keyboard_Class::KeysState st = M5Cardputer.Keyboard.keysState();
    KeySnap now;
    if (st.fn)    now.add(KC_FN);
    if (st.shift) now.add(KC_SHIFT);
    if (st.ctrl)  now.add(KC_CTRL);
    if (st.opt)   now.add(KC_OPT);
    if (st.alt)   now.add(KC_ALT);
    if (st.tab)   now.add(KC_TAB);
    if (st.del)   now.add(KC_DEL);
    if (st.enter) now.add(KC_ENTER);
    if (st.space) now.add(KC_SPACE);
    for (auto k : st.word) now.add((uint8_t)normalizeKey(k));

    // -- newly pressed --
    for (uint8_t i = 0; i < now.n; i++) {
        uint8_t kc = now.codes[i];
        if (s_prev.has(kc)) continue;

        // piano/pads take priority over any action bound to the same key
        if (pianoSemi(kc) >= 0 &&
            (g_curPage == PAGE_PATTERN || g_curPage == PAGE_SOUND)) {
            doPiano(kc, now);
            continue;
        }
        const int8_t sampleKey = samplePerformanceKey(kc);
        if (g_curPage == PAGE_SAMPLE && sampleKey >= 0) {
            s_selectedSampleSlice = static_cast<uint8_t>(sampleKey);
            liveSampleHit(g_streamSampleSlot, static_cast<uint8_t>(sampleKey));
            continue;
        }
        if (g_curPage == PAGE_CHORD && chordKeyDegree(kc) >= 0) {
            playChord(kc, now);
            continue;
        }
        if (g_curPage == PAGE_MEDO && doMedoKey(kc, now)) continue;

        uint8_t act = keyAction(kc);
        if (act == ACT_NONE) continue;

        // PO-33 punch effects are momentary performance keys. They engage on
        // key-down and return dry on release; WRITE/PLAY captures the selected
        // effect at the same quantized pattern step as notes and samples.
        if (g_curPage == PAGE_KO && act >= ACT_PAT1 && act <= ACT_PAT8) {
            const uint8_t index = static_cast<uint8_t>(act - ACT_PAT1);
            g_poEffectSelection = static_cast<uint8_t>(g_patternBank * 8u + index);
            g_poLiveEffectActive = true;
            s_poEffectKey = kc;
            g_poEffectProcessor.engage(static_cast<PoEffect>(g_poEffectSelection));
            if (g_recEnabled || g_playing)
                g_poPatternEffects.set(g_playing ? g_playPattern : g_curPattern,
                    sequencerPatternRecordStep(), static_cast<PoEffect>(g_poEffectSelection));
            g_needRedraw = true;
            continue;
        }

        if (actImmediate(act)) {
            doImmediate(act, now);
            if (actRepeats(act)) {
                s_rptAct = act; s_rptKc = kc;
                s_rptNext = nowMs + RPT_DELAY_MS; s_rptCount = 0;
            }
        } else if (s_nHolds < 8) {
            s_holds[s_nHolds++] = { nowMs, kc, act, false };
        }
    }

    // -- mic capture ends when AUX is released --
    if (g_recPadKc != KC_NONE && !now.has(g_recPadKc)) {
        g_recPadKc = KC_NONE;
        micRecStop();
    }

    // Release drives both outbound MIDI and the MGX/FM4 ADSR release stage.
    // MG/303 intentionally ignores the internal note-off.
    for (uint8_t index = 0; index < s_prev.n; ++index)
        if (!now.has(s_prev.codes[index])) {
            if (s_prev.codes[index] == s_poEffectKey) {
                s_poEffectKey = KC_NONE;
                g_poLiveEffectActive = false;
                g_poEffectProcessor.engage(PO_FX_NONE);
                g_needRedraw = true;
            }
            releaseMidiNote(s_prev.codes[index]);
            if (g_curPage == PAGE_CHORD ||
                (g_curPage == PAGE_MEDO && g_medoPerformance.role() == MEDO_CHORD))
                releaseChord(s_prev.codes[index]);
        }

    // -- released --
    for (uint8_t i = 0; i < s_nHolds; ) {
        if (!now.has(s_holds[i].kc)) {
            if (!s_holds[i].fired) doShort(s_holds[i].act);
            s_holds[i] = s_holds[--s_nHolds];
        } else i++;
    }
    if (s_rptAct != ACT_NONE && !now.has(s_rptKc)) { s_rptAct = ACT_NONE; s_rptCount = 0; }

    s_prev = now;
}
