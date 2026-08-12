// ============================================================
// Microgroove - ui.cpp
// 240x135, sprite double-buffered. 6 pages.
// Drum grid drawn TR-style: kick (lane 1) at the bottom.
// Footer doubles as long-press progress bar / mic level meter.
// ============================================================
#include <M5Cardputer.h>
#include <SD.h>
#include "config.h"
#include "ui.h"
#include "sequencer.h"
#include "sampler.h"
#include "storage.h"
#include "wavetable.h"
#include "audio_engine.h"
#include "sd_diagnostics.h"
#include "master_recorder.h"
#include "mic_sampler.h"
#include "stem_recorder.h"
#include "sampler_slots.h"
#include "streaming_sampler.h"
#include "loop_engine.h"
#include "event_looper.h"
#include "motion.h"
#include "ble_midi.h"
#include "usb_midi.h"
#include "sd_io_arbiter.h"
#include "synth_parameters.h"
#include "synth_ui_model.h"
#include "boot_selector.h"
#include "performance_state.h"

Page    g_curPage    = PAGE_PATTERN;
bool    g_needRedraw = true;
uint8_t g_soundParam = 0;
SynthSoundBank g_soundBank = SYNTH_BANK_LEGACY;
uint8_t g_songCursor = 0;
uint8_t g_streamSampleSlot = 0;
uint8_t g_streamSampleMode = SAMPLER_SLOT_MELODIC;
uint8_t g_sampleEditMode = 0;
uint8_t g_sampleParam = 0;
uint8_t g_loopCursor = 0;
uint8_t g_eventCursor = 0;
uint8_t g_motionCursor = 0;
float   g_holdProg   = 0.0f;
char    g_holdLabel[16] = "";

char    g_fileList[BROWSER_MAX][SAMPLE_NAME_LEN];
uint8_t g_fileCount = 0;
uint8_t g_fileSel   = 0;

static M5Canvas canvas(&M5Cardputer.Display);
static char     s_status[24] = "";
static uint32_t s_statusUntil = 0;

static const char* noteNames[] = {"--","C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
static const uint16_t trackCols[4] = { COL_SYNTH1, COL_SYNTH2, COL_SYNTH3, COL_DRUMS };
static const char* engNames[]  = {"808","909","SMP"};
static const char* type808[]   = {"KICK","SNARE","CHAT","CLAP"};
static const char* type909[]   = {"KICK","SNARE","CHAT","OHAT"};
static const char* oscNames[]  = {"SAW","SQR","TRI","SIN","WT"};

void uiInit() {
    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.fillScreen(TFT_BLACK);
    // The ADV has no PSRAM.  An 8-bit canvas preserves the existing buffered
    // UI while cutting its heap allocation from 64.8 KiB to 32.4 KiB.
    canvas.setColorDepth(8);
    canvas.createSprite(SCREEN_W, SCREEN_H);
    canvas.setTextFont(1);
    canvas.setTextSize(1);
}

void uiStatus(const char* msg) {
    strncpy(s_status, msg, sizeof(s_status) - 1);
    s_status[sizeof(s_status) - 1] = 0;
    s_statusUntil = millis() + 1500;
    g_needRedraw = true;
}

void uiScanSampleDir() {
    g_fileCount = 0;
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return;
    File dir;
    { SdIoGuard guard; dir = SD.open(DIR_SAMPLES); }
    if (!dir || !dir.isDirectory()) return;
    File f;
    { SdIoGuard guard; f = dir.openNextFile(); }
    while (f && g_fileCount < BROWSER_MAX) {
        String nm;
        bool isDirectory = false;
        { SdIoGuard guard; nm = f.name(); isDirectory = f.isDirectory(); }
        int slash = nm.lastIndexOf('/');            // some core versions return full path
        if (slash >= 0) nm = nm.substring(slash + 1);
        if (!isDirectory && (nm.endsWith(".wav") || nm.endsWith(".WAV"))) {
            strncpy(g_fileList[g_fileCount], nm.c_str(), SAMPLE_NAME_LEN - 1);
            g_fileList[g_fileCount][SAMPLE_NAME_LEN - 1] = 0;
            g_fileCount++;
        }
        { SdIoGuard guard; f.close(); f = dir.openNextFile(); }
    }
    { SdIoGuard guard; dir.close(); }
    if (g_fileSel >= g_fileCount) g_fileSel = 0;
}

// ---------- header / footer ----------
static void drawHeader() {
    canvas.fillRect(0, 0, SCREEN_W, 11, COL_GRID);
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 2);
    static const char* pageNames[] = {
        "PATTERN","SOUND","FX","VOCODER","CHORD","SAMPLE","KO","LOOPS","EVENT","MEDO",
        "MOTION","SONG","SD TEST","HELP"
    };
    canvas.print(pageNames[g_curPage]);

    canvas.setCursor(60, 2);
    canvas.printf("BPM%3u", g_bpm);

    canvas.setCursor(104, 2);
    canvas.printf("PT%u", g_curPattern + 1);
    if (g_songMode) { canvas.setTextColor(COL_ACCENT); canvas.print(" SNG"); }

    canvas.setTextColor(COL_DIM);
    canvas.setCursor(158, 2);
    const int battery = M5.Power.getBatteryLevel();
    canvas.printf("%cP%u O%u B%u", g_patternBank ? 'B' : 'A', g_curProject + 1,
                  g_curOctave, static_cast<unsigned>(constrain(battery, 0, 100)));

    // transport
    if (g_playing) { canvas.setTextColor(COL_SYNTH2); canvas.setCursor(206, 2); canvas.print(">"); }
    if (g_recEnabled) canvas.fillCircle(222, 5, 3, COL_REC);
}

static void drawFooter(const char* hint) {
    // long-press progress / mic level meter takes over the footer
    if (g_holdProg > 0.001f) {
        int w = (int)((SCREEN_W - 4) * (g_holdProg > 1.0f ? 1.0f : g_holdProg));
        canvas.drawRect(1, SCREEN_H - 10, SCREEN_W - 2, 9, COL_GRID);
        canvas.fillRect(2, SCREEN_H - 9, w, 7, COL_ACCENT);
        if (g_holdLabel[0]) {
            canvas.setTextColor(COL_TEXT);
            canvas.setCursor(4, SCREEN_H - 9);
            canvas.print(g_holdLabel);
        }
        return;
    }
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(2, SCREEN_H - 9);
    if (millis() < s_statusUntil) {
        canvas.setTextColor(COL_ACCENT);
        canvas.print(s_status);
    } else {
        canvas.print(hint);
    }
}

// ---------- PATTERN page ----------
static void drawPatternPage() {
    const int gx = 14, gw = 14;          // grid origin x, cell width
    int y = 14;

    // 3 synth rows (14 px tall)
    for (int t = 0; t < NUM_SYNTHS; t++) {
        canvas.setTextColor(g_synthMute[t] ? COL_GRID : trackCols[t]);
        canvas.setCursor(2, y + 3);
        canvas.printf("%d", t + 1);

        for (int st = 0; st < NUM_STEPS; st++) {
            int x = gx + st * gw;
            const SynthCell& c = g_patterns[g_curPattern].synth[t][st];
            uint16_t bg = COL_BG;
            if (g_playing && g_playPattern == g_curPattern &&
                st == (g_playStep + NUM_STEPS - 1) % NUM_STEPS) bg = 0x39E7;
            if (c.accent && !c.empty()) bg = 0x6180;   // dark orange
            canvas.fillRect(x, y, gw - 1, 12, bg);
            canvas.drawRect(x, y, gw - 1, 12, (st % 4 == 0) ? COL_DIM : COL_GRID);

            if (!c.empty()) {
                canvas.setTextColor(g_synthMute[t] ? COL_DIM : trackCols[t]);
                canvas.setCursor(x + 1, y + 2);
                canvas.print(noteNames[c.note[0]]);
                // chord: one dot per extra note, top-right of the cell
                for (int nn = 1; nn < MAX_POLY; nn++)
                    if (c.note[nn] != NOTE_EMPTY)
                        canvas.fillRect(x + gw - 3, y + 1 + (nn - 1) * 3, 2, 2,
                                        g_synthMute[t] ? COL_DIM : trackCols[t]);
                if (c.slide) canvas.drawFastHLine(x + 1, y + 10, gw - 3, COL_ACCENT);
            }
            if (t == g_curTrack && st == g_curStep)
                canvas.drawRect(x - 1, y - 1, gw + 1, 14, COL_CURSOR);
        }
        y += 15;
    }

    // 8 drum lanes, TR-style: lane 8 on top, kick (lane 1) at the bottom
    y += 1;
    for (int row = 0; row < NUM_DRUM_LANES; row++) {
        int l = NUM_DRUM_LANES - 1 - row;             // visual row -> lane
        DrumLane& d = g_drumLanes[l];
        bool sel = (g_curTrack == NUM_SYNTHS && g_curDrumLane == l);
        canvas.setTextColor(g_drumMute ? COL_GRID : (sel ? COL_TEXT : COL_DRUMS));
        canvas.setCursor(2, y);
        canvas.printf("%d", l + 1);

        for (int st = 0; st < NUM_STEPS; st++) {
            int x = gx + st * gw;
            bool hit = g_patterns[g_curPattern].drums[st] & (1 << l);
            uint16_t col = hit ? (g_drumMute ? COL_DIM : COL_DRUMS) : COL_GRID;
            if (g_playing && g_playPattern == g_curPattern &&
                st == (g_playStep + NUM_STEPS - 1) % NUM_STEPS && hit) col = COL_TEXT;
            canvas.fillRect(x + 3, y + 1, gw - 7, 5, col);
            if (sel && st == g_curStep)
                canvas.drawRect(x + 1, y - 1, gw - 3, 9, COL_CURSOR);
        }
        // engine tag
        canvas.setTextColor(COL_DIM);
        canvas.setCursor(gx + NUM_STEPS * gw + 2, y);
        canvas.print(d.engine == ENG_SMPL ? "S" : (d.engine == ENG_909 ? "9" : "8"));
        y += 8;
    }
}

// ---------- SOUND page ----------
static void drawBar(int x, int y, float v01) {
    canvas.drawRect(x, y, 62, 7, COL_GRID);
    canvas.fillRect(x + 1, y + 1, (int)(60 * v01), 5, COL_SYNTH1);
}

static bool synthBarParameter(SynthParameter parameter) {
    switch (parameter) {
        case SYNTH_PARAM_VOLUME:
        case SYNTH_PARAM_MG_CUTOFF:
        case SYNTH_PARAM_MG_RESONANCE:
        case SYNTH_PARAM_MG_FILTER_ENV:
        case SYNTH_PARAM_MG_FILTER_DECAY:
        case SYNTH_PARAM_MG_AMP_DECAY:
        case SYNTH_PARAM_MGX_CUTOFF:
        case SYNTH_PARAM_MGX_RESONANCE:
        case SYNTH_PARAM_MGX_FILTER_ENV:
        case SYNTH_PARAM_MGX_PULSE_WIDTH:
        case SYNTH_PARAM_MGX_SUB_LEVEL:
        case SYNTH_PARAM_MGX_DRIVE:
        case SYNTH_PARAM_MGX_VELOCITY_AMP:
        case SYNTH_PARAM_MGX_VELOCITY_FILTER:
        case SYNTH_PARAM_MGX_AMP_SUSTAIN:
        case SYNTH_PARAM_MGX_FILTER_SUSTAIN:
        case SYNTH_PARAM_MGX_LFO_DEPTH:
        case SYNTH_PARAM_FM_FEEDBACK:
            return true;
        default:
            if (parameter >= SYNTH_PARAM_FM_OP1_LEVEL && parameter < SYNTH_PARAM_COUNT) {
                const uint8_t field = static_cast<uint8_t>(
                    parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u;
                return field == 1u || field == 4u;
            }
            return false;
    }
}

static void drawSynthParameterValue(const SynthTrack& track,
                                    SynthParameter parameter, int y) {
    int32_t value = 0;
    if (!synthGetParameter(track, parameter, value)) {
        canvas.print("?");
        return;
    }
    if (parameter == SYNTH_PARAM_ENGINE) canvas.print(synthEngineName(track.displayEngine()));
    else if (parameter == SYNTH_PARAM_MG_OSC || parameter == SYNTH_PARAM_MGX_OSC)
        canvas.print(oscNames[value]);
    else if (parameter == SYNTH_PARAM_MG_WAVETABLE ||
             parameter == SYNTH_PARAM_MGX_WAVETABLE) {
        canvas.print(value < g_numWavetables ? g_wtNames[value] : "WT?");
        const OscMode mode = parameter == SYNTH_PARAM_MG_WAVETABLE
            ? track.v[0].oscMode : track.mgxPatch.oscMode;
        if (mode != OSC_WT) { canvas.setTextColor(COL_GRID); canvas.print(" (off)"); }
    } else if (parameter == SYNTH_PARAM_MGX_FILTER_MODE)
        canvas.print(synthFilterModeName(static_cast<SynthFilterMode>(value)));
    else if (parameter == SYNTH_PARAM_MGX_LFO_DESTINATION)
        canvas.print(synthLfoDestinationName(static_cast<SynthLfoDestination>(value)));
    else if (parameter == SYNTH_PARAM_VOICES)
        canvas.printf("%ld %s", static_cast<long>(value),
                      value > 1 ? "POLY" : "MONO");
    else if (synthBarParameter(parameter)) drawBar(64, y, value * 0.01f);
    else if (parameter == SYNTH_PARAM_MGX_LFO_RATE)
        canvas.printf("%ld.%02ld Hz", static_cast<long>(value / 100),
                      static_cast<long>(value % 100));
    else if (parameter == SYNTH_PARAM_FM_INDEX)
        canvas.printf("%ld.%02ld", static_cast<long>(value / 100),
                      static_cast<long>(value % 100));
    else if (parameter >= SYNTH_PARAM_FM_OP1_RATIO && parameter < SYNTH_PARAM_COUNT &&
             ((parameter - SYNTH_PARAM_FM_OP1_RATIO) % 6u) == 0u)
        canvas.printf("%ld.%02ld", static_cast<long>(value / 100),
                      static_cast<long>(value % 100));
    else if ((parameter >= SYNTH_PARAM_MGX_AMP_ATTACK &&
              parameter <= SYNTH_PARAM_MGX_AMP_RELEASE) ||
             (parameter >= SYNTH_PARAM_MGX_FILTER_ATTACK &&
              parameter <= SYNTH_PARAM_MGX_FILTER_RELEASE) ||
             parameter >= SYNTH_PARAM_FM_OP1_ATTACK)
        canvas.printf("%ld ms", static_cast<long>(value));
    else if (parameter == SYNTH_PARAM_FM_ALGORITHM)
        canvas.printf("A%ld", static_cast<long>(value + 1));
    else canvas.printf("%ld", static_cast<long>(value));
}

static void drawSoundPage() {
    bool onDrums = (g_curTrack == NUM_SYNTHS);
    // P3: keep the displayed bank valid for the (possibly just-requested)
    // engine, so out-of-band engine changes can't leave stale banks on screen.
    if (!onDrums)
        g_soundBank = synthEnsureSoundBank(
            g_synths[g_curTrack].displayEngine(), g_soundBank);
    canvas.setTextColor(trackCols[g_curTrack]);
    canvas.setCursor(2, 14);
    if (onDrums) {
        DrumLane& d = g_drumLanes[g_curDrumLane];
        canvas.printf("DRUM LANE %d  [%s]", g_curDrumLane + 1, engNames[d.engine]);
    } else {
        canvas.printf("SYNTH %d %s [%s]", g_curTrack + 1,
                      synthEngineName(g_synths[g_curTrack].displayEngine()),
                      synthSoundBankName(g_soundBank));
    }

    // scope
    canvas.drawRect(150, 24, 88, 44, COL_GRID);
    int prevY = 46;
    for (int i = 0; i < 86 && i * 2 < g_scopeIdx; i++) {
        int yy = 46 - (int)(g_scopeBuf[i * 2] * 20.0f);
        yy = constrain(yy, 25, 66);
        canvas.drawLine(151 + i - 1, prevY, 151 + i, yy, COL_SYNTH2);
        prevY = yy;
    }
    g_scopeIdx = 0;

    int y = 26;
    char val[24];

    if (!onDrums) {
        SynthTrack& trk = g_synths[g_curTrack];
        const uint8_t rows = synthSoundBankRows(g_soundBank);
        for (uint8_t r = 0; r < rows; r++) {
            const SynthUiRow item = synthSoundBankRow(g_soundBank, r);
            bool sel = (g_soundParam == r);
            canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.printf("%s%-8s", sel ? ">" : " ", item.label);
            drawSynthParameterValue(trk, item.parameter, y);
            y += 11;    // 9 rows must clear the footer bar
        }
    } else {
        DrumLane& d = g_drumLanes[g_curDrumLane];
        const char* names[] = {"LANE","ENGINE","TYPE","VOLUME","TUNE","DECAY","CHOKE"};
        for (int r = 0; r < 7; r++) {
            bool sel = (g_soundParam == r);
            canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.printf("%s%-8s", sel ? ">" : " ", names[r]);
            switch (r) {
                case 0: canvas.printf("%d", g_curDrumLane + 1); break;
                case 1: canvas.print(engNames[d.engine]); break;
                case 2:
                    if (d.engine == ENG_SMPL) {
                        const char* name = samplerReferenceName(d.sampleSlot);
                        if (name[0]) canvas.print(name);
                        else { canvas.setTextColor(COL_GRID); canvas.print("(no sample)"); }
                    } else {
                        canvas.print(d.engine == ENG_909 ? type909[d.type] : type808[d.type]);
                    }
                    break;
                case 3: drawBar(64, y, d.volume); break;
                case 4: snprintf(val, sizeof(val), "%+.1f st", d.tune); canvas.print(val); break;
                case 5: snprintf(val, sizeof(val), "%.2fx", d.decay);   canvas.print(val); break;
                case 6: if (d.chokeGroup) canvas.printf("GRP %u", d.chokeGroup);
                        else canvas.print("OFF");
                        break;
            }
            y += 12;
        }
    }
}

static void drawChordPage() {
    static const char* modeNames[HICHORD_MODE_COUNT] = {
        "PLAY","STRUM","LEAD","DRONE","ARP","REPEAT","MIC SAMPLE","DRUM",
        "DRUM LOOPS","AUTO DRUM","SEQUENCER","CHORD HIRO","EAR TRAIN","TUNER","MIXER"
    };
    static const char* mapNames[] = {"DEFAULT","EXTENDED","CHROMATIC"};
    static const char* bassNames[] = {"OFF","ROOT","SLASH"};
    static const char* keyNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    const char* labels[] = {"MODE","KEY","SCALE","CHORD MAP","OCTAVE","BASS","VOICE LEAD","INVERSION"};
    canvas.setTextColor(COL_TEXT); canvas.setCursor(2, 15);
    canvas.printf("7-CHORD INSTRUMENT  DEGREE %u", g_chordDegree + 1);
    for (uint8_t row = 0; row < 8; ++row) {
        const int y = 29 + row * 11;
        canvas.setTextColor(row == g_chordParameter ? COL_TEXT : COL_DIM);
        const char* label = labels[row];
        if (row == 7 && g_hiChordPerformance.mode() == HICHORD_DRUM) label = "DRUM KIT";
        else if (row == 7 && (g_hiChordPerformance.mode() == HICHORD_DRUM_LOOPS ||
                 g_hiChordPerformance.mode() == HICHORD_AUTO_DRUM)) label = "VARIATION";
        else if (row == 7 && g_hiChordPerformance.mode() == HICHORD_CHORD_HIRO) label = "SONG";
        else if (row == 7 && g_hiChordPerformance.mode() == HICHORD_EAR_TRAINER) label = "LEVEL";
        else if (row == 7 && g_hiChordPerformance.mode() == HICHORD_ARPEGGIO) label = "ARP PATT";
        else if (row == 7 && g_hiChordPerformance.mode() == HICHORD_REPEAT) label = "RATE";
        canvas.setCursor(2, y); canvas.printf("%c %-10s", row == g_chordParameter ? '>' : ' ', label);
        switch (row) {
            case 0: canvas.print(modeNames[g_hiChordPerformance.mode()]); break;
            case 1: canvas.print(keyNames[g_chordSettings.key]); break;
            case 2: canvas.print(ChordEngine::scaleName(g_chordSettings.scale)); break;
            case 3: canvas.print(mapNames[g_chordSettings.map]); break;
            case 4: canvas.printf("%d", g_chordSettings.octave); break;
            case 5: canvas.print(bassNames[g_chordSettings.bassMode]); break;
            case 6: canvas.print(g_chordSettings.voiceLeading ? "ON" : "OFF"); break;
            case 7:
                if (g_hiChordPerformance.mode() == HICHORD_DRUM) canvas.printf("%u/7", g_hiChordDrumKit + 1);
                else if (g_hiChordPerformance.mode() == HICHORD_DRUM_LOOPS ||
                         g_hiChordPerformance.mode() == HICHORD_AUTO_DRUM)
                    canvas.printf("%u/8", g_hiChordGrooveVariation + 1);
                else if (g_hiChordPerformance.mode() == HICHORD_CHORD_HIRO)
                    canvas.print(hiChordPracticeSong(g_hiChordPracticeSong).name);
                else if (g_hiChordPerformance.mode() == HICHORD_EAR_TRAINER)
                    canvas.printf("%u score:%u", g_hiChordEarLevel + 1, g_hiChordEarScore);
                else if (g_hiChordPerformance.mode() == HICHORD_ARPEGGIO) {
                    static const char* arpNames[] = {"UP","DOWN","UP/DN","DN/UP","RANDOM","CHORD"};
                    canvas.printf("%s x%u L%u", arpNames[g_hiChordArpPattern],
                                  g_hiChordArpRate, g_hiChordArpLayer + 1);
                } else if (g_hiChordPerformance.mode() == HICHORD_REPEAT)
                    canvas.printf("x%u", g_hiChordRepeatRate);
                else canvas.printf("%+d", g_chordSettings.inversion[g_chordDegree]);
                break;
        }
    }
    canvas.setTextColor(COL_SYNTH2); canvas.setCursor(146, 32); canvas.print("fn sh a s d f g");
    canvas.setTextColor(COL_DIM); canvas.setCursor(146, 44); canvas.print("I II III IV V VI VII");
    canvas.setCursor(146, 61); canvas.print("hold x/c/v/b");
    canvas.setCursor(146, 72); canvas.print("for chord map");
    if (g_hiChordPerformance.mode() == HICHORD_TUNER) {
        const PitchEstimate pitch = micTunerEstimate();
        canvas.setTextColor(pitch.midiNote >= 0 ? COL_SYNTH2 : COL_DIM);
        canvas.setCursor(146, 88);
        if (pitch.midiNote >= 0) canvas.printf("%.1fHz N%d %+dc", pitch.hz,
                                               pitch.midiNote, pitch.cents);
        else canvas.print("hold AUX + play");
    }
}

static void drawFxPage() {
    static const char* names[MASTER_EFFECT_COUNT] = {
        "REVERB","DELAY","CHORUS","FLANGER","TREMOLO","VIBRATO","FILTER"
    };
    const MasterEffectType effect = static_cast<MasterEffectType>(g_masterEffectSelection);
    const MasterEffectsSettings fx = g_masterEffects.settings();
    const char* labels[] = {"EFFECT","ENABLED","MIX","FEEDBACK","RATE","FILTER"};
    canvas.setTextColor(COL_TEXT); canvas.setCursor(2, 15); canvas.print("HICHORD MASTER EFFECTS");
    for (uint8_t row = 0; row < 6; ++row) {
        const int y = 32 + row * 15;
        canvas.setTextColor(row == g_masterEffectParameter ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y); canvas.printf("%c %-10s", row == g_masterEffectParameter ? '>' : ' ', labels[row]);
        if (row == 0) canvas.print(names[effect]);
        else if (row == 1) canvas.print(g_masterEffects.enabled(effect) ? "ON" : "OFF");
        else if (row == 2) canvas.printf("%u", fx.mix[effect]);
        else if (row == 3) canvas.printf("%u", fx.feedback);
        else if (row == 4) canvas.printf("%u", fx.rate);
        else canvas.printf("%u", fx.filter);
    }
}

static void drawVocoderPage() {
    static const char* sourceNames[] = {"LOOP 1","ONBOARD MIC","AUDIO CAP LINE"};
    const VocoderSettings v = g_vocoder.settings();
    const char* labels[] = {"ENABLED","SOURCE","FORMANT","Q","ATTACK","RELEASE","NOISE","GATE"};
    canvas.setTextColor(COL_TEXT); canvas.setCursor(2, 15); canvas.print("8-BAND VOCODER");
    for (uint8_t row = 0; row < 8; ++row) {
        const int y = 29 + row * 12;
        canvas.setTextColor(row == g_vocoderParameter ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y); canvas.printf("%c %-9s", row == g_vocoderParameter ? '>' : ' ', labels[row]);
        if (row == 0) canvas.print(v.enabled ? "ON" : "OFF");
        else if (row == 1) canvas.print(sourceNames[v.source]);
        else if (row == 2) canvas.printf("%+d st", v.formantShift);
        else if (row == 3) canvas.printf("%u", v.resonance);
        else if (row == 4) canvas.printf("%u", v.attack);
        else if (row == 5) canvas.printf("%u", v.release);
        else if (row == 6) canvas.printf("%u", v.noise);
        else canvas.printf("%u", v.gate);
    }
    if (v.source != VOCODER_LOOP1) {
        canvas.setTextColor(COL_ACCENT); canvas.setCursor(144, 102);
        canvas.print("source needs hardware");
    }
}

static const char *poEffectName(PoEffect effect) {
    static const char* names[PO_FX_COUNT] = {
        "LOOP 16","LOOP 12","LOOP SHORT","LOOP TINY","UNISON","UNISON LOW",
        "OCTAVE UP","OCTAVE DOWN","STUTTER 4","STUTTER 3","SCRATCH","SCRATCH FAST",
        "6/8 QUANT","RETRIGGER","REVERSE","NO EFFECT"
    };
    return names[effect < PO_FX_COUNT ? effect : PO_FX_NONE];
}

static void drawKoPage() {
    canvas.setTextColor(COL_TEXT); canvas.setCursor(2, 15);
    canvas.printf("PO-STYLE PUNCH FX PT%u ST%u SW%u", g_curPattern + 1, g_curStep + 1, g_swing);
    canvas.setTextColor(COL_ACCENT); canvas.setCursor(2, 31);
    canvas.printf("> %02u %s", g_poEffectSelection + 1,
                  poEffectName(static_cast<PoEffect>(g_poEffectSelection)));
    canvas.setTextColor(COL_DIM); canvas.setCursor(2, 47);
    canvas.printf("PLAYING: %s", poEffectName(g_poEffectProcessor.effect()));
    canvas.setCursor(2, 62);
    canvas.printf("STEP LOCK: %s", poEffectName(g_poPatternEffects.get(g_curPattern, g_curStep)));
    for (uint8_t i = 0; i < 8; ++i) {
        const uint8_t effect = static_cast<uint8_t>(g_patternBank * 8 + i);
        const int x = 4 + (i % 4) * 58, y = 82 + (i / 4) * 15;
        canvas.setTextColor(effect == g_poEffectSelection ? COL_TEXT : COL_GRID);
        canvas.setCursor(x, y); canvas.printf("%u:%.7s", i + 1, poEffectName(static_cast<PoEffect>(effect)));
    }
}

static void drawMedoPage() {
    static const char* roles[] = {"DRUM","BASS","CHORD","LEAD","SAMPLE"};
    static const char* quantize[] = {"AS PLAYED","SNAP 16","MEDO GROOVE"};
    static const char* scales[] = {"NATURAL","PENTA MAJOR","PENTA MINOR"};
    static const char* arp[] = {"UP","DOWN","UP/DOWN","RANDOM"};
    const MedoRole role = g_medoPerformance.role();
    const MedoTrackSettings &settings = g_medoPerformance.settings(role);
    canvas.setTextColor(COL_TEXT); canvas.setCursor(2, 15);
    canvas.print("MEDO FIVE-ROLE PERFORMANCE");
    const char* labels[] = {"ROLE","QUANTIZE","VOLUME","OCTAVE","LOOP BARS","SCALE","ARP"};
    for (uint8_t row = 0; row < 7; ++row) {
        const int y = 28 + row * 13;
        canvas.setTextColor(row == g_medoParameter ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y); canvas.printf("%c %-10s", row == g_medoParameter ? '>' : ' ', labels[row]);
        if (row == 0) canvas.print(roles[role]);
        else if (row == 1) canvas.print(quantize[settings.quantize]);
        else if (row == 2) canvas.printf("%u", settings.volume);
        else if (row == 3) canvas.printf("%+d", settings.octave);
        else if (row == 4) canvas.printf("%u all roles", g_medoPerformance.sharedBars());
        else if (row == 5) canvas.print(scales[g_medoPerformance.scale()]);
        else canvas.printf("%s x%u", arp[g_medoPerformance.arpDirection()], g_medoPerformance.arpRate());
    }
    canvas.setTextColor(COL_DIM); canvas.setCursor(2, 121);
    canvas.printf("%u bars  %u events  click/press/slide + IMU",
                  g_eventLooper.bars(role), g_eventLooper.count(role));
}

// ---------- SAMPLE page ----------
static void drawSamplePage() {
    const StreamingSamplerSnapshot sampler = streamingSamplerSnapshot();
    const SamplerSlot& streamSlot = g_samplerSlotBank.slot(g_streamSampleSlot);
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 14);
    canvas.printf("SLOT %u %s %s", g_streamSampleSlot + 1,
                  g_streamSampleMode == SAMPLER_SLOT_SLICED ? "SLICE" : "MELO",
                  g_sampleEditMode == 0 ? "BROWSE" :
                  g_sampleEditMode == 1 ? "SOUND" : "LOCK");

    // pool usage
    float use = static_cast<float>(g_samplerSlotBank.quotaUsedFrames()) /
                static_cast<float>(SAMPLER_QUOTA_FRAMES);
    canvas.setCursor(130, 14);
    canvas.setTextColor(COL_DIM);
    canvas.printf("40s %2d%%", (int)(use * 100));
    canvas.drawRect(180, 13, 58, 8, COL_GRID);
    canvas.fillRect(181, 14, (int)(56 * use), 6, use > 0.9f ? COL_REC : COL_SYNTH2);

    const bool recording = sampler.recordState == STREAM_SAMPLE_REC_STARTING ||
                           sampler.recordState == STREAM_SAMPLE_REC_RECORDING ||
                           sampler.recordState == STREAM_SAMPLE_REC_STOPPING;
    if (recording) {
        canvas.setCursor(2, 25);
        canvas.setTextColor(COL_REC);
        canvas.printf("REC S%u %s %4.1f/%4.1fs drop:%lu",
                      sampler.recordSlot + 1,
                      sampler.recordInput == STREAM_SAMPLE_INPUT_MIC ? "MIC" : "BUS",
                      sampler.recordFrames / static_cast<float>(
                          sampler.recordInput == STREAM_SAMPLE_INPUT_MIC ? MIC_RATE : SAMPLE_RATE),
                      sampler.recordTargetFrames / static_cast<float>(
                          sampler.recordInput == STREAM_SAMPLE_INPUT_MIC ? MIC_RATE : SAMPLE_RATE),
                      static_cast<unsigned long>(sampler.recordDroppedFrames));
    }

    if (g_sampleEditMode != 0) {
        canvas.setCursor(2, recording ? 37 : 25);
        canvas.setTextColor(streamSlot.mode == SAMPLER_SLOT_EMPTY ? COL_GRID : COL_SYNTH2);
        canvas.printf("%s  PAT%u STEP%u %s", streamSlot.mode == SAMPLER_SLOT_EMPTY
                      ? "(empty)" : streamSlot.filename, g_curPattern + 1, g_curStep + 1,
                      g_samplerSequence.findLock(g_curPattern, g_curStep,
                                                 g_streamSampleSlot) ? "LOCKED" : "");
        if (streamSlot.mode == SAMPLER_SLOT_EMPTY) return;
        const SamplerLockEntry* lock = g_samplerSequence.findLock(
            g_curPattern, g_curStep, g_streamSampleSlot);
        const char* names[] = {"PITCH", "GAIN", "CUTOFF", "RESONANCE", "TRIM START", "TRIM LENGTH"};
        int y = recording ? 52 : 40;
        for (uint8_t row = 0; row < 6; ++row) {
            canvas.setTextColor(row == g_sampleParam ? COL_TEXT : COL_DIM);
            canvas.setCursor(2, y);
            canvas.printf("%c %-11s", row == g_sampleParam ? '>' : ' ', names[row]);
            if (g_sampleEditMode == 1) {
                if (row == 0) canvas.printf("%+.1f st", streamSlot.pitchQ8 / 256.0f);
                else if (row == 1) canvas.printf("%3u%%", streamSlot.gainQ15 * 100u / 32767u);
                else if (row == 2) canvas.printf("%3u%%", streamSlot.cutoffQ15 * 100u / 32767u);
                else if (row == 3) canvas.printf("%3u%%", streamSlot.resonanceQ15 * 100u / 32767u);
                else if (row == 4) canvas.printf("%.2fs", streamSlot.trimStart /
                                                  static_cast<float>(streamSlot.sourceRate));
                else canvas.printf("%.2fs", streamSlot.trimLength /
                                    static_cast<float>(streamSlot.sourceRate));
            } else if (!lock) {
                canvas.print("--");
            } else if (row == 0) canvas.printf("%+.1f st", lock->pitchQ8 / 256.0f);
            else if (row == 1) canvas.printf("%3u%%", lock->gainQ15 * 100u / 32767u);
            else if (row == 2) canvas.printf("%3u%%", lock->cutoffQ15 * 100u / 32767u);
            else if (row == 3) canvas.printf("%3u%%", lock->resonanceQ15 * 100u / 32767u);
            else if (row == 4) canvas.printf("%3u%%", lock->trimStartQ15 * 100u / 32767u);
            else canvas.printf("%3u%%", lock->trimLengthQ15 * 100u / 32767u);
            y += 13;
        }
        return;
    }

    if (g_fileCount == 0 && !recording) {
        canvas.setTextColor(COL_DIM);
        canvas.setCursor(2, 40);
        canvas.print("Put .wav files in");
        canvas.setCursor(2, 52);
        canvas.print(DIR_SAMPLES);
        canvas.setCursor(2, 70);
        canvas.print("or hold AUX to mic-sample");
        return;
    }

    canvas.setCursor(2, recording ? 37 : 25);
    canvas.setTextColor(streamSlot.mode == SAMPLER_SLOT_EMPTY ? COL_GRID : COL_SYNTH2);
    canvas.printf("STREAM: %s", streamSlot.mode == SAMPLER_SLOT_EMPTY
                  ? "(empty)" : streamSlot.filename);

    // scrolling list, 7 rows
    int first = (g_fileSel > 3) ? g_fileSel - 3 : 0;
    if (first + 8 > g_fileCount) first = max(0, (int)g_fileCount - 8);
    int y = recording ? 50 : 38;
    const int rows = recording ? 6 : 7;
    for (int i = first; i < first + rows && i < g_fileCount; i++) {
        bool sel = (i == g_fileSel);
        bool loaded = samplerFindByName(g_fileList[i]) >= 0;
        canvas.setTextColor(sel ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y);
        canvas.printf("%s%s", sel ? ">" : " ", g_fileList[i]);
        if (loaded) { canvas.setTextColor(COL_SYNTH2); canvas.setCursor(210, y); canvas.print("RAM"); }
        y += 12;
    }
}

static void drawLoopsPage() {
    const LoopEngineSnapshot loops = loopEngineSnapshot();
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 15);
    canvas.printf("6 AUDIO LOOPS%s%s %.1fs", loops.paused ? " PAUSE" : "",
                  loops.metronome ? " MET" : "", loops.timelineFrames /
                  static_cast<float>(SAMPLE_RATE));
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(145, 15);
    canvas.printf("R/W %lu/%lu", static_cast<unsigned long>(loops.maxReadUs / 1000),
                  static_cast<unsigned long>(loops.maxWriteUs / 1000));
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        const LoopStreamTrackSnapshot& item = loops.tracks[track];
        const int y = 30 + track * 15;
        canvas.setTextColor(track == g_loopCursor ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y);
        canvas.printf("%c L%u%s %-9s %3u%% %4.1fs U%lu D%lu",
                      track == g_loopCursor ? '>' : ' ', track + 1,
                      loops.soloTrack == track ? "S" : " ",
                      loopEngineStateName(item.state),
                      static_cast<unsigned>(item.volumeQ15) * 100u / 32767u,
                      item.lengthFrames / static_cast<float>(SAMPLE_RATE),
                      static_cast<unsigned long>(item.underruns),
                      static_cast<unsigned long>(item.droppedFrames));
    }
}

static void drawEventPage() {
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 15);
    canvas.printf("5-PART EVENT LOOP  %u/%u", g_eventLooper.count(),
                  EVENT_LOOP_CAPACITY);
    for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
        const EventLoopTrackState& state = g_eventLooper.track(track);
        const int y = 32 + track * 17;
        canvas.setTextColor(track == g_eventCursor ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y);
        canvas.printf("%c %u %-7s %3ubar %4uev %s%s",
                      track == g_eventCursor ? '>' : ' ', track + 1,
                      eventLooperRoleName(track), g_eventLooper.bars(track),
                      g_eventLooper.count(track), state.armed ? "REC " : "",
                      state.muted ? "MUTE" : "");
    }
}

static void drawMotionPage() {
    const MotionSnapshot motion = motionSnapshot();
    const UsbMidiSnapshot usb = usbMidiSnapshot();
    const BleMidiSnapshot ble = bleMidiSnapshot();
    canvas.setTextColor(motion.available ? COL_SYNTH2 : COL_REC);
    canvas.setCursor(2, 15);
    canvas.printf("BMI270 %s  USB:%s BLE:%s", motion.available ? "ON" : "OFF",
                  usb.mounted ? "MIDI" : "--", ble.connected ? "MIDI" : "--");
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(2, 28);
    canvas.printf("X%3u Y%3u A%3u G%3u gesture:%02X",
                  motion.values[MOTION_SOURCE_TILT_X], motion.values[MOTION_SOURCE_TILT_Y],
                  motion.values[MOTION_SOURCE_ACCEL], motion.values[MOTION_SOURCE_GYRO],
                  motion.gestures);
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const int y = 48 + mapping * 18;
        canvas.setTextColor(mapping == g_motionCursor ? COL_TEXT : COL_DIM);
        canvas.setCursor(2, y);
        canvas.printf("%c M%u %-7s > %s", mapping == g_motionCursor ? '>' : ' ',
                      mapping + 1, motionSourceName(motion.mappings[mapping].source),
                      motionTargetName(motion.mappings[mapping].target));
    }
}

// ---------- SONG page ----------
static void drawSongPage() {
    const MasterRecorderSnapshot master = masterRecorderSnapshot();
    const StemRecorderSnapshot stems = stemRecorderSnapshot();
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 14);
    const uint8_t pageBase = (g_songCursor / 64) * 64;
    canvas.printf("SONG %s %u-%u", g_songMode ? "[ON]" : "[off]",
                  pageBase + 1, pageBase + 64);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(120, 14);
    canvas.printf("PROJECT P%u %s", g_curProject + 1,
                  storageProjectExists(g_curProject) ? "*" : "");
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) {
        canvas.setTextColor(COL_REC);
        canvas.setCursor(2, 112);
        if (masterRecorderIsBusy())
            canvas.printf("MASTER %s %lus D%lu", masterRecorderStateName(master.state),
                          static_cast<unsigned long>(master.framesWritten / SAMPLE_RATE),
                          static_cast<unsigned long>(master.droppedFrames));
        else
            canvas.printf("STEMS %s %lus D%lu", stemRecorderStateName(stems.state),
                          static_cast<unsigned long>(stems.framesWritten / SAMPLE_RATE),
                          static_cast<unsigned long>(stems.droppedFrames));
    }

    const int gx = 8, gy = 28, cw = 14, ch = 16;
    for (int visible = 0; visible < 64; visible++) {
        const int i = pageBase + visible;
        int x = gx + (visible % 16) * cw;
        int y = gy + (visible / 16) * ch;
        bool isCursor = (i == g_songCursor);
        bool isPlay   = (g_songMode && g_playing && i == g_songPos);

        canvas.drawRect(x, y, cw - 1, ch - 2, isPlay ? COL_TEXT : COL_GRID);
        if (i == g_songLoopStart)
            canvas.drawFastVLine(x, y, ch - 2, COL_ACCENT);

        if (g_song[i] != SONG_EMPTY) {
            canvas.setTextColor(isPlay ? COL_TEXT : COL_SYNTH1);
            canvas.setCursor(x + 4, y + 4);
            static const char patternGlyphs[NUM_PATTERNS + 1] = "123456789ABCDEFG";
            canvas.print(patternGlyphs[g_song[i]]);
        }
        if (isCursor) canvas.drawRect(x - 1, y - 1, cw + 1, ch, COL_CURSOR);
    }
}

// ---------- SD TEST page ----------
static void drawDiagPage() {
    const SdDiagSnapshot diag = sdDiagnosticsSnapshot();
    const char* state = diag.state == SD_DIAG_IDLE ? "READY" :
                        diag.state == SD_DIAG_RUNNING ? "RUNNING" :
                        diag.state == SD_DIAG_PASS ? "PASS" : "FAIL";
    const uint16_t stateColor = diag.state == SD_DIAG_PASS ? COL_SYNTH2 :
                                diag.state == SD_DIAG_FAIL ? COL_REC : COL_ACCENT;

    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(2, 16);
    canvas.print("4 KiB CARDPUTER-ADV STORAGE TEST");
    canvas.setTextColor(stateColor);
    canvas.setCursor(2, 30);
    canvas.printf("%s  %s", state, diag.step);

    canvas.setTextColor(COL_DIM);
    canvas.setCursor(2, 48); canvas.printf("WRITE       %5lu KB/s", (unsigned long)diag.writeKBs);
    canvas.setCursor(2, 60); canvas.printf("SEQ READ    %5lu KB/s", (unsigned long)diag.readKBs);
    canvas.setCursor(2, 72); canvas.printf("6-FILE READ %5lu KB/s", (unsigned long)diag.roundRobinKBs);
    canvas.setCursor(2, 88); canvas.printf("MAX W/R %lu / %lu us",
                                           (unsigned long)diag.maxWriteUs,
                                           (unsigned long)diag.maxReadUs);
    canvas.setCursor(2, 100); canvas.printf("MIN HEAP %lu  ERR %lu",
                                            (unsigned long)diag.minFreeHeap,
                                            (unsigned long)diag.errors);
}

// ---------- HELP page ----------
static void drawHelpPage() {
    static const char* lines[] = {
        "spc play/stop (hold=from top)",
        "ctl page  / action z clear xcvb move",
        "` 1 2 3 tracks (hold=mute)",
        "4..- patterns; tab bank A/B",
        "= load  del save  (both=demo)",
        "opt/alt bpm-+ (hold=octave/prj)",
        "SAMPLE: tab browse/sound/lock",
        "LOOP: /=rec .=mute x/b=volume",
        "EVENT /=arm  MOTION .=target",
        "SONG hold .=master n=stems",
    };
    int y = 16;
    canvas.setTextColor(COL_DIM);
    for (auto l : lines) { canvas.setCursor(2, y); canvas.print(l); y += 11; }
}

// ---------- draw dispatch ----------
void uiDraw() {
    canvas.fillSprite(COL_BG);
    drawHeader();
    switch (g_curPage) {
        case PAGE_PATTERN: drawPatternPage();
            drawFooter("ctl:page /:rec spc:play"); break;
        case PAGE_SOUND:   drawSoundPage();
            drawFooter("v c row  x b adjust  m=fine"); break;
        case PAGE_FX:      drawFxPage();
            drawFooter("v/c:row x/b:value /=toggle"); break;
        case PAGE_VOCODER: drawVocoderPage();
            drawFooter("v/c:row x/b:value /=toggle | L1 works now"); break;
        case PAGE_CHORD:   drawChordPage();
            drawFooter("fn..g chords | hold x/c/v/b + chord"); break;
        case PAGE_SAMPLE:  drawSamplePage();
            drawFooter(g_sampleEditMode == 0
                       ? "tab:edit /=assign =copy del=paste hold .=mic"
                       : g_sampleEditMode == 1
                       ? "tab:lock arrows:edit m+x/b:step"
                       : "tab:browse arrows:lock /=clear ,=step clr"); break;
        case PAGE_KO:      drawKoPage();
            drawFooter("4..-:FX tab:bank /=engage n=swing .=off"); break;
        case PAGE_LOOPS:   drawLoopsPage();
            drawFooter("/=rec .=mute tab=solo n=pause ,=metro"); break;
        case PAGE_EVENT:   drawEventPage();
            drawFooter("v/c:track x/b:bars /=arm .:mute"); break;
        case PAGE_MEDO:    drawMedoPage();
            drawFooter("v/c:row x/b:value z:clear role"); break;
        case PAGE_MOTION:  drawMotionPage();
            drawFooter("v/c:map x/b:source .:target z:clear"); break;
        case PAGE_SONG:    drawSongPage();
            drawFooter("4..-:set z:clr | hold .=master n=stems"); break;
        case PAGE_DIAG:    drawDiagPage();
            drawFooter("/:run  keep music playing"); break;
        default:           drawHelpPage();
            drawFooter("MICROGROOVE"); break;
    }
    canvas.pushSprite(0, 0);
}

void uiSplash() {
    const BootSelectorSnapshot boot = bootSelectorSnapshot();
    const BootRole current = boot.layout.compiledRole;
    const BootRole other = bootOtherRole(current);
    const bool otherValid = other == BOOT_ROLE_NORMAL
        ? boot.layout.normalValid : boot.layout.usbHostValid;
    canvas.fillSprite(COL_BG);
    canvas.setTextSize(2);
    canvas.setTextColor(COL_SYNTH1); canvas.setCursor(15, 12); canvas.print("MINI STUDIO");
    canvas.setTextColor(COL_DRUMS);  canvas.setCursor(173, 12); canvas.print("16");
    canvas.setTextSize(1);
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(15, 37); canvas.print("Microgroove engine + expanded studio");
    canvas.setTextColor(current == BOOT_ROLE_USB_HOST ? COL_DRUMS : COL_SYNTH2);
    canvas.setCursor(15, 56);
    canvas.printf("MODE: %s", current == BOOT_ROLE_USB_HOST ? "USB MIDI HOST" : "NORMAL USB DEVICE");
    canvas.setTextColor(COL_DIM);
    canvas.setCursor(15, 72);
    if (!boot.layoutMatchesBuild) {
        canvas.print("Standalone/recovery image; switching disabled");
    } else if (!otherValid) {
        canvas.print("Other role is not installed in flash");
    } else {
        canvas.printf("TAB: switch to %s + reboot",
                      other == BOOT_ROLE_USB_HOST ? "USB HOST" : "NORMAL");
    }
    canvas.setTextColor(COL_ACCENT);
    canvas.setCursor(15, 108); canvas.print("Any other key: start");
    canvas.pushSprite(0, 0);
}

void uiBootMessage(const char* title, const char* detail) {
    canvas.fillSprite(COL_BG);
    canvas.setTextSize(2);
    canvas.setTextColor(COL_ACCENT);
    canvas.setCursor(15, 24);
    canvas.print(title ? title : "BOOT");
    canvas.setTextSize(1);
    canvas.setTextColor(COL_TEXT);
    canvas.setCursor(15, 62);
    canvas.print(detail ? detail : "");
    canvas.pushSprite(0, 0);
}
