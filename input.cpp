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

void inputInit();
void inputUpdate();

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
struct Hold { uint8_t kc; uint8_t act; uint32_t t0; bool fired; };
static Hold    s_holds[8];
static uint8_t s_nHolds = 0;

// auto-repeat state (one repeating key at a time is plenty)
static uint8_t  s_rptAct  = ACT_NONE;
static uint8_t  s_rptKc   = KC_NONE;
static uint32_t s_rptNext = 0;
static uint16_t s_rptCount = 0;

struct HeldMidiNote { uint8_t key, channel, note; };
static HeldMidiNote s_heldMidiNotes[16];
static uint8_t s_heldMidiNoteCount = 0;

// ---------- helpers ----------
static bool accentHeld(const KeySnap& s) { return s.has('m'); }

static void rememberMidiNote(uint8_t key, uint8_t channel, uint8_t note) {
    for (uint8_t index = 0; index < s_heldMidiNoteCount; ++index) {
        if (s_heldMidiNotes[index].key != key) continue;
        s_heldMidiNotes[index] = {key, channel, note};
        return;
    }
    if (s_heldMidiNoteCount < sizeof(s_heldMidiNotes) / sizeof(s_heldMidiNotes[0]))
        s_heldMidiNotes[s_heldMidiNoteCount++] = {key, channel, note};
}

static void releaseMidiNote(uint8_t key) {
    for (uint8_t index = 0; index < s_heldMidiNoteCount; ++index) {
        if (s_heldMidiNotes[index].key != key) continue;
        liveSynthRelease(s_heldMidiNotes[index].channel,
                         s_heldMidiNotes[index].note);
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
                const uint32_t amount = min<uint32_t>(256, slot.sourceFrames);
                const int next = constrain(static_cast<int>(slot.trimStart) +
                                           direction * static_cast<int>(amount),
                                           0, static_cast<int>(slot.sourceFrames - SAMPLER_SLICE_COUNT));
                const uint32_t length = min<uint32_t>(slot.trimLength,
                                                      slot.sourceFrames - next);
                g_samplerSlotBank.setTrim(g_streamSampleSlot, next,
                                          max<uint32_t>(SAMPLER_SLICE_COUNT, length));
                break;
            }
            case 5: {
                const int next = constrain(static_cast<int>(slot.trimLength) + direction * 256,
                                           static_cast<int>(SAMPLER_SLICE_COUNT),
                                           static_cast<int>(slot.sourceFrames - slot.trimStart));
                g_samplerSlotBank.setTrim(g_streamSampleSlot, slot.trimStart, next);
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
            if (dx) {
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
                source = (source + (MOTION_SOURCE_COUNT - 1) + dx) %
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
            if (g_curPage == PAGE_SAMPLE) {
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
                    uiStatus(loopEngineRequestRecord(g_loopCursor) ? "LOOP ARMED" : "RECORD FAILED");
            } else if (g_curPage == PAGE_EVENT) {
                const bool armed = !g_eventLooper.track(g_eventCursor).armed;
                g_eventLooper.setArmed(g_eventCursor, armed);
                uiStatus(armed ? "EVENT REC ARMED" : "EVENT REC OFF");
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
            if (g_playing) sequencerStop(); else sequencerStart(false);
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
        if (g_curPage == PAGE_SAMPLE || g_curPage == PAGE_LOOPS ||
            g_curPage == PAGE_EVENT || g_curPage == PAGE_MOTION ||
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
                g_curPage == PAGE_SAMPLE) {
                const bool started = g_curPage == PAGE_SAMPLE
                    ? micStreamRecStart(g_streamSampleSlot,
                                        static_cast<SamplerSlotMode>(g_streamSampleMode))
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
    } else {
        bool legato = heldPianoCount(s_prev) >= 1;
        liveSynthNote(g_curTrack, note, oct, accent, legato);
    }
    rememberMidiNote(kc, g_curTrack, midiNote);
}

// ---------- main entry ----------
void inputInit() {
    s_prev = KeySnap();
    s_nHolds = 0; s_rptAct = ACT_NONE;
    s_heldMidiNoteCount = 0;
    g_holdProg = 0; g_holdLabel[0] = 0;
}

void inputUpdate() {
    M5Cardputer.update();
    uint32_t nowMs = millis();

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
            liveSampleHit(g_streamSampleSlot, static_cast<uint8_t>(sampleKey));
            continue;
        }

        uint8_t act = keyAction(kc);
        if (act == ACT_NONE) continue;

        if (actImmediate(act)) {
            doImmediate(act, now);
            if (actRepeats(act)) {
                s_rptAct = act; s_rptKc = kc;
                s_rptNext = nowMs + RPT_DELAY_MS; s_rptCount = 0;
            }
        } else if (s_nHolds < 8) {
            s_holds[s_nHolds++] = { kc, act, nowMs, false };
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
        if (!now.has(s_prev.codes[index])) releaseMidiNote(s_prev.codes[index]);

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
