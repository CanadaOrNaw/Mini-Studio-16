// ============================================================
// CardputerGroovebox - sequencer.cpp
// ============================================================
#include "sequencer.h"
#include <Arduino.h>
#include <string.h>
#include "sampler_slots.h"
#include "streaming_sampler.h"
#include "event_looper.h"
#include "motion.h"

Pattern    g_patterns[NUM_PATTERNS];
uint8_t    g_song[SONG_LENGTH];
uint8_t    g_songLoopStart = 0;
uint8_t    g_patternBank = 0;

SynthTrack g_synths[NUM_SYNTHS];
DrumLane   g_drumLanes[NUM_DRUM_LANES];
bool       g_synthMute[NUM_SYNTHS] = {false, false, false};
bool       g_drumMute = false;

volatile bool g_playing = false;
bool       g_recEnabled = false;
bool       g_songMode   = false;
uint8_t    g_playStep    = 0;
uint8_t    g_playPattern = 0;
uint8_t    g_songPos     = 0;
uint16_t   g_bpm         = 128;

uint8_t    g_curPattern  = 0;
uint8_t    g_curTrack    = 0;
uint8_t    g_curStep     = 0;
uint8_t    g_curDrumLane = 0;
uint8_t    g_curOctave   = 4;

extern bool g_needRedraw;   // owned by ui.cpp

static uint32_t s_stepUs      = 0;
static uint32_t s_lastStepUs  = 0;
static bool     s_externalClock = false;
static uint32_t s_stepPeriod() { return 60000000UL / g_bpm / 4; }   // 16th notes

void sequencerInit() {
    memset(g_patterns, 0, sizeof(g_patterns));
    memset(g_song, SONG_EMPTY, sizeof(g_song));

    for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].init();
    g_synths[0].forEach([](SynthVoice& v){ v.oscMode = OSC_SAW; v.fltCutoff = 0.30f;
                                           v.fltReso = 0.45f;   v.fltEnvAmt = 0.60f; });
    g_synths[1].forEach([](SynthVoice& v){ v.oscMode = OSC_SQR; v.fltCutoff = 0.45f;
                                           v.volume  = 0.6f; });
    g_synths[2].forEach([](SynthVoice& v){ v.oscMode = OSC_WT;  v.wtIndex = 0;
                                           v.fltCutoff = 0.55f; v.volume = 0.5f; });
    g_synths[2].setVoices(3);          // track 3 ships polyphonic (chords/pads)

    // lanes 0-3: 808 KICK SNARE CHAT CLAP | lanes 4-7: 909 KICK SNARE CHAT OHAT
    g_drumLanes[0].init(ENG_808, DT_KICK);
    g_drumLanes[1].init(ENG_808, DT_SNARE);
    g_drumLanes[2].init(ENG_808, DT_HAT_C);
    g_drumLanes[3].init(ENG_808, DT_HAT_O_OR_CLAP);
    g_drumLanes[4].init(ENG_909, DT_KICK);
    g_drumLanes[5].init(ENG_909, DT_SNARE);
    g_drumLanes[6].init(ENG_909, DT_HAT_C);
    g_drumLanes[7].init(ENG_909, DT_HAT_O_OR_CLAP);
    // 909 hats choke each other by default
    g_drumLanes[6].chokeGroup = 1;
    g_drumLanes[7].chokeGroup = 1;
    eventLooperInit();
}

// ---------- triggering ----------
static void triggerLane(uint8_t lane) {
    if (lane >= NUM_DRUM_LANES) return;
    uint8_t grp = g_drumLanes[lane].chokeGroup;
    if (grp != 0) {
        for (int i = 0; i < NUM_DRUM_LANES; i++)
            if (i != lane && g_drumLanes[i].chokeGroup == grp)
                g_drumLanes[i].choke();
    }
    g_drumLanes[lane].trigger();
}

static void triggerStep(uint8_t step) {
    Pattern& p = g_patterns[g_playPattern];

    for (int s = 0; s < NUM_SYNTHS; s++) {
        if (g_synthMute[s]) continue;
        const SynthCell& c = p.synth[s][step];
        bool mono = (g_synths[s].voices <= 1);
        for (int i = 0; i < (mono ? 1 : MAX_POLY); i++)
            if (c.note[i] != NOTE_EMPTY)
                g_synths[s].noteOn(noteToFreq(c.note[i], c.oct[i]),
                                   c.accent, mono && c.slide);
    }

    if (!g_drumMute) {
        uint8_t mask = p.drums[step];
        for (int l = 0; l < NUM_DRUM_LANES; l++)
            if (mask & (1 << l)) triggerLane(l);
    }

    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        const uint8_t key = g_samplerSequence.eventKey(g_playPattern, step, slot);
        if (key == 0xFF) continue;
        streamingSamplerTrigger(slot, key,
            g_samplerSequence.findLock(g_playPattern, step, slot));
    }
}

static void triggerEvent(const EventLoopEvent& event) {
    switch (event.type) {
        case EVENT_LOOP_NOTE: {
            if (event.target >= NUM_SYNTHS || event.value1 < 12 || event.value1 > 127)
                break;
            const uint8_t note = static_cast<uint8_t>((event.value1 % 12) + 1);
            const uint8_t octave = static_cast<uint8_t>((event.value1 / 12) - 1);
            g_synths[event.target].noteOn(noteToFreq(note, octave),
                                          event.value2 >= 100, false);
            break;
        }
        case EVENT_LOOP_DRUM:
            triggerLane(event.target);
            break;
        case EVENT_LOOP_SAMPLE:
            streamingSamplerTrigger(event.target, event.value1);
            break;
        case EVENT_LOOP_CONTROL:
            motionApplyRecordedControl(event.target, event.value1);
            break;
        default:
            break;
    }
}

// ---------- transport ----------
static void prepareStart(bool fromTop) {
    if (fromTop) {
        g_playStep = 0;
        if (g_songMode) {
            g_songPos = g_songLoopStart;
            if (g_song[g_songPos] != SONG_EMPTY) g_playPattern = g_song[g_songPos];
        }
        eventLooperResetTransport();
    }
    if (!g_songMode) g_playPattern = g_curPattern;
    g_playing = true;
}

void sequencerStart(bool fromTop) {
    s_externalClock = false;
    prepareStart(fromTop);
    s_lastStepUs = micros() - s_stepPeriod();   // fire step immediately
}

void sequencerStop() {
    s_externalClock = false;
    g_playing = false;
    g_playStep = 0;
    eventLooperResetTransport();
}

static void songAdvance() {
    // find next non-empty slot; wrap to loop start
    uint8_t start = g_songPos;
    for (int i = 0; i < SONG_LENGTH; i++) {
        g_songPos = (g_songPos + 1) % SONG_LENGTH;
        if (g_songPos == 0 && start != SONG_LENGTH - 1) g_songPos = g_songLoopStart;
        if (g_song[g_songPos] != SONG_EMPTY) { g_playPattern = g_song[g_songPos]; return; }
        if (g_songPos == start) break;   // nothing found
    }
    // fallback: stay on current
}

static void advanceOneStep() {
    triggerStep(g_playStep);
    g_eventLooper.forStep(g_eventLoopPosition, triggerEvent);
    eventLooperAdvance();
    g_playStep++;
    if (g_playStep >= NUM_STEPS) {
        g_playStep = 0;
        if (g_songMode) songAdvance();
        else            g_playPattern = g_curPattern;  // pattern switch on bar
    }
    g_needRedraw = true;
}

void sequencerTick() {
    if (!g_playing || s_externalClock) return;
    uint32_t now = micros();
    if (now - s_lastStepUs < s_stepPeriod()) return;
    s_lastStepUs += s_stepPeriod();                 // accumulate: no drift
    advanceOneStep();
}

void sequencerExternalStart(bool fromTop) {
    s_externalClock = true;
    prepareStart(fromTop);
}

void sequencerExternalStop() { g_playing = false; }

void sequencerExternalStep() {
    if (s_externalClock && g_playing) advanceOneStep();
}

void sequencerExternalSongPosition(uint16_t position) {
    g_playStep = static_cast<uint8_t>(position % NUM_STEPS);
    if (g_songMode) {
        g_songPos = static_cast<uint8_t>((position / NUM_STEPS) % SONG_LENGTH);
        if (g_song[g_songPos] != SONG_EMPTY) g_playPattern = g_song[g_songPos];
    }
    eventLooperSetPosition(position);
    g_needRedraw = true;
}

uint16_t sequencerEventRecordStep() {
    uint16_t previous = g_eventLoopPosition == 0
        ? EVENT_LOOP_MAX_STEPS - 1 : g_eventLoopPosition - 1;
    if (s_externalClock) return previous;
    return (micros() - s_lastStepUs > s_stepPeriod() / 2)
        ? g_eventLoopPosition : previous;
}

// ---------- live input ----------
// quantize: current step if within first half, else next step
static uint8_t quantizedStep() {
    if (s_externalClock)
        return g_playStep == 0 ? NUM_STEPS - 1 : g_playStep - 1;
    uint32_t elapsed = micros() - s_lastStepUs;
    uint8_t  step    = g_playStep == 0 ? NUM_STEPS - 1 : g_playStep - 1;  // step just triggered
    if (elapsed > s_stepPeriod() / 2) step = g_playStep;                   // round up
    return step % NUM_STEPS;
}

void liveSynthNote(uint8_t track, uint8_t note, uint8_t octave, bool accent, bool legato) {
    if (track >= NUM_SYNTHS) return;
    bool poly = (g_synths[track].voices > 1);
    // poly: legato means "chord", not slide
    g_synths[track].noteOn(noteToFreq(note, octave), accent, !poly && legato);
    const uint8_t midi = static_cast<uint8_t>((octave + 1u) * 12u + note - 1u);
    eventLooperRecordSynth(sequencerEventRecordStep(), track, midi,
                           accent ? 127 : 96);

    if (g_recEnabled) {
        if (g_playing) {
            uint8_t st = quantizedStep();
            SynthCell& c = g_patterns[g_playPattern].synth[track][st];
            if (poly && legato && !c.empty()) { c.add(note, octave); if (accent) c.accent = true; }
            else c.setMono(note, octave, accent, !poly && legato);
        } else {
            // step-write: keys held together stack a chord on the same step
            if (poly && legato) {
                uint8_t prev = (uint8_t)((g_curStep + NUM_STEPS - 1) % NUM_STEPS);
                SynthCell& c = g_patterns[g_curPattern].synth[track][prev];
                if (!c.empty()) { c.add(note, octave); if (accent) c.accent = true;
                                  g_needRedraw = true; return; }
            }
            g_patterns[g_curPattern].synth[track][g_curStep].setMono(note, octave, accent, false);
            g_curStep = (g_curStep + 1) % NUM_STEPS;   // step-write advance
        }
        g_needRedraw = true;
    }
}

void liveDrumHit(uint8_t lane) {
    triggerLane(lane);
    eventLooperRecordDrum(sequencerEventRecordStep(), lane, 127);
    if (g_recEnabled) {
        if (g_playing) {
            g_patterns[g_playPattern].drums[quantizedStep()] |= (1 << lane);
        } else {
            g_patterns[g_curPattern].drums[g_curStep] |= (1 << lane);
            g_curStep = (g_curStep + 1) % NUM_STEPS;
        }
        g_needRedraw = true;
    }
}

// ---------- helpers for the one-key layout / sampling ----------
void clonePatternTo(uint8_t dst) {
    if (dst >= NUM_PATTERNS) return;
    if (dst != g_curPattern) g_patterns[dst] = g_patterns[g_curPattern];
}

void triggerLaneLive(uint8_t lane) {
    triggerLane(lane);
}

// ---------- step editing ----------
void toggleDrumAtCursor(uint8_t lane) {
    g_patterns[g_curPattern].drums[g_curStep] ^= (1 << lane);
    g_needRedraw = true;
}

void writeSynthAtCursor(uint8_t note, uint8_t octave, bool accent) {
    if (g_curTrack >= NUM_SYNTHS) return;
    g_patterns[g_curPattern].synth[g_curTrack][g_curStep].setMono(note, octave, accent, false);
    g_curStep = (g_curStep + 1) % NUM_STEPS;
    g_needRedraw = true;
}

void clearCellAtCursor() {
    if (g_curTrack < NUM_SYNTHS)
        g_patterns[g_curPattern].synth[g_curTrack][g_curStep].clear(g_curOctave);
    else
        g_patterns[g_curPattern].drums[g_curStep] = 0;
    g_needRedraw = true;
}

void clearStepAtPlayhead() {
    uint8_t st = g_playStep == 0 ? NUM_STEPS - 1 : g_playStep - 1;
    if (g_curTrack < NUM_SYNTHS)
        g_patterns[g_playPattern].synth[g_curTrack][st].clear(g_curOctave);
    else
        g_patterns[g_playPattern].drums[st] = 0;
    g_needRedraw = true;
}

// ---------- demo ----------
void loadDemoPattern() {
    Pattern& p = g_patterns[g_curPattern];
    memset(&p, 0, sizeof(p));

    // acid bassline on synth 1
    const uint8_t bl[NUM_STEPS]  = {1,0,1,0, 4,0,1,0, 1,0,8,0, 6,0,4,0};
    const bool    acc[NUM_STEPS] = {1,0,0,0, 1,0,0,0, 0,0,1,0, 0,0,1,0};
    const bool    sld[NUM_STEPS] = {0,0,0,0, 0,0,1,0, 0,0,0,0, 1,0,0,0};
    for (int i = 0; i < NUM_STEPS; i++)
        if (bl[i]) p.synth[0][i].setMono(bl[i], 2, acc[i], sld[i]);

    // poly chord stabs on synth 3 (Am: A-C-E), showing off VOICES=3
    p.synth[2][0].setMono(10, 3, false, false);            // A3
    p.synth[2][0].add(1, 4); p.synth[2][0].add(5, 4);      // + C4 E4
    p.synth[2][8] = p.synth[2][0];

    // 909 kick 4-on-floor (lane 4), 808 clap on 2&4 (lane 3), 909 hats offbeat (lane 6)
    for (int i = 0; i < NUM_STEPS; i += 4)  p.drums[i] |= (1 << 4);
    p.drums[4] |= (1 << 3); p.drums[12] |= (1 << 3);
    for (int i = 2; i < NUM_STEPS; i += 4)  p.drums[i] |= (1 << 6);
    for (int i = 0; i < NUM_STEPS; i += 2)  p.drums[i] |= (1 << 2);
}
