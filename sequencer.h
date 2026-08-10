// ============================================================
// CardputerGroovebox - sequencer.h
// Patterns, transport, step triggering, song chain, live record.
// ============================================================
#pragma once
#include "config.h"
#include "synth_voice.h"
#include "drum_voice.h"

// Up to MAX_POLY notes per step. Slot 0 is "the" note for mono
// tracks; slots 1..2 are chord notes on poly tracks.
struct SynthCell {
    uint8_t note[MAX_POLY];   // 0 = empty, 1..12 = C..B
    uint8_t oct [MAX_POLY];   // 1..7
    bool    accent;
    bool    slide;            // played back only when track VOICES == 1

    inline bool empty() const { return note[0] == NOTE_EMPTY; }
    inline void clear(uint8_t defOct) {
        for (int i = 0; i < MAX_POLY; i++) { note[i] = NOTE_EMPTY; oct[i] = defOct; }
        accent = false; slide = false;
    }
    inline void setMono(uint8_t n, uint8_t o, bool a, bool s) {
        clear(o); note[0] = n; oct[0] = o; accent = a; slide = s;
    }
    // add a chord note; returns slot used (replaces last slot when full)
    inline uint8_t add(uint8_t n, uint8_t o) {
        for (int i = 0; i < MAX_POLY; i++)                 // same note: re-write
            if (note[i] == n && oct[i] == o) return i;
        for (int i = 0; i < MAX_POLY; i++)
            if (note[i] == NOTE_EMPTY) { note[i] = n; oct[i] = o; return i; }
        note[MAX_POLY - 1] = n; oct[MAX_POLY - 1] = o;
        return MAX_POLY - 1;
    }
};

// A synth track = 1..MAX_POLY identical voices sharing parameters.
// voices == 1 -> exact legacy mono/303 behavior incl. slide+legato.
struct SynthTrack {
    SynthVoice v[MAX_POLY];
    uint8_t    voices;        // 1..MAX_POLY
    uint8_t    rr;            // round-robin cursor

    void init() { for (int i = 0; i < MAX_POLY; i++) v[i].init(); voices = 1; rr = 0; }

    // apply a parameter edit to every voice (params are per-track)
    template <typename F> void forEach(F f) { for (int i = 0; i < MAX_POLY; i++) f(v[i]); }

    void setVoices(uint8_t n) {
        if (n < 1) n = 1;
        if (n > MAX_POLY) n = MAX_POLY;
        voices = n;
        for (int i = n; i < MAX_POLY; i++) v[i].active = false;  // kill spares
        if (n > 1) rr %= n;
    }

    // mono path keeps slide semantics; poly allocates a voice
    void noteOn(float freq, bool acc, bool slide) {
        if (voices <= 1) { v[0].noteOn(freq, acc, slide); return; }
        int pick = -1;
        for (int i = 0; i < voices; i++)                   // free voice first
            if (!v[i].active) { pick = i; break; }
        if (pick < 0) {                                    // steal quietest
            float amp = 1e9f; 
            for (int i = 0; i < voices; i++)
                if (v[i].ampEnv < amp) { amp = v[i].ampEnv; pick = i; }
        }
        rr = (uint8_t)((pick + 1) % voices);
        v[pick].noteOn(freq, acc, false);                  // no slide in poly
    }

    inline float render() {
        if (voices <= 1) return v[0].render();
        float s = 0;
        for (int i = 0; i < voices; i++) s += v[i].render();
        return s * (voices > 2 ? 0.62f : 0.75f);           // headroom vs. clip
    }
};

struct Pattern {
    SynthCell synth[NUM_SYNTHS][NUM_STEPS];
    uint8_t   drums[NUM_STEPS];   // bitmask, bit n = lane n
};

// ---------- Shared state (defined in sequencer.cpp) ----------
extern Pattern    g_patterns[NUM_PATTERNS];
extern uint8_t    g_song[SONG_LENGTH];      // pattern indices, SONG_EMPTY = empty
extern uint8_t    g_songLoopStart;
extern uint8_t    g_patternBank;          // 0 = patterns 1..8, 1 = 9..16

extern SynthTrack g_synths[NUM_SYNTHS];
extern DrumLane   g_drumLanes[NUM_DRUM_LANES];
extern bool       g_synthMute[NUM_SYNTHS];
extern bool       g_drumMute;

// transport
extern volatile bool g_playing;
extern bool       g_recEnabled;
extern bool       g_songMode;
extern uint8_t    g_playStep;
extern uint8_t    g_playPattern;    // pattern currently sounding
extern uint8_t    g_songPos;
extern uint16_t   g_bpm;

// edit state
extern uint8_t    g_curPattern;     // pattern being edited/viewed
extern uint8_t    g_curTrack;       // 0..2 synth, 3 = drums
extern uint8_t    g_curStep;
extern uint8_t    g_curDrumLane;    // selected lane 0..7
extern uint8_t    g_curOctave;

void sequencerInit();
void sequencerStart(bool fromTop);
void sequencerStop();
void sequencerTick();               // call every loop(); handles step timing
void sequencerExternalStart(bool fromTop);
void sequencerExternalStop();
void sequencerExternalStep();
void sequencerExternalSongPosition(uint16_t position);
uint16_t sequencerEventRecordStep();

// Live input -> sound + optional record
void liveSynthNote(uint8_t track, uint8_t note, uint8_t octave, bool accent, bool legato);
void liveDrumHit(uint8_t lane);

// pattern + lane helpers (new-layout / sampling additions)
void clonePatternTo(uint8_t dst);   // copy current pattern into slot dst
void triggerLaneLive(uint8_t lane); // audition a lane (choke-aware, no record)

// step editing helpers
void toggleDrumAtCursor(uint8_t lane);
void writeSynthAtCursor(uint8_t note, uint8_t octave, bool accent);
void clearCellAtCursor();
void clearStepAtPlayhead();         // held-delete erase while recording

void loadDemoPattern();
