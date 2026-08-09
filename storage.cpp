// ============================================================
// CardputerGroovebox - storage.cpp
// ============================================================
#include "storage.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include <SD.h>
#include <string.h>

uint8_t g_curProject = 0;

#define GBX_MAGIC   0x31584247u   // "GBX1"
#define GBX_VERSION 3             // v3: 16 patterns + 128-entry chain
#define LEGACY_NUM_PATTERNS 8
#define LEGACY_SONG_LENGTH  64

// ---- serialized structures (POD, packed layout kept stable) ----
struct __attribute__((packed)) SaveSynth {
    uint8_t oscMode, wtIndex;
    float   cutoff, reso, envAmt, ampDec, filtDec, volume;
};

struct __attribute__((packed)) SaveDrumLane {
    uint8_t engine, type, chokeGroup;
    float   volume, tune, decay;
    char    sampleName[SAMPLE_NAME_LEN];   // empty = none
};

// ---------- v1 (legacy, mono cells) ----------
struct __attribute__((packed)) SaveCellV1 {
    uint8_t note, octave, flags;   // bit0 accent, bit1 slide
};
struct __attribute__((packed)) ProjectFileV1 {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    uint8_t  song[LEGACY_SONG_LENGTH];
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveCellV1   cells[LEGACY_NUM_PATTERNS][NUM_SYNTHS][NUM_STEPS];
    uint8_t      drums[LEGACY_NUM_PATTERNS][NUM_STEPS];
};

// ---------- v2 (poly cells + voices) ----------
struct __attribute__((packed)) SaveCell {
    uint8_t note[MAX_POLY];
    uint8_t oct [MAX_POLY];
    uint8_t flags;                 // bit0 accent, bit1 slide
};
struct __attribute__((packed)) ProjectFileV2 {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    uint8_t  song[LEGACY_SONG_LENGTH];
    uint8_t  voices[NUM_SYNTHS];   // 1..MAX_POLY per synth track
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveCell     cells[LEGACY_NUM_PATTERNS][NUM_SYNTHS][NUM_STEPS];
    uint8_t      drums[LEGACY_NUM_PATTERNS][NUM_STEPS];
};

struct __attribute__((packed)) ProjectFileV3 {
    uint32_t magic;
    uint16_t version;
    uint16_t bpm;
    uint8_t  songLoopStart;
    uint8_t  song[SONG_LENGTH];
    uint8_t  voices[NUM_SYNTHS];
    SaveSynth    synths[NUM_SYNTHS];
    SaveDrumLane lanes[NUM_DRUM_LANES];
    SaveCell     cells[NUM_PATTERNS][NUM_SYNTHS][NUM_STEPS];
    uint8_t      drums[NUM_PATTERNS][NUM_STEPS];
};

union ProjectBuffer {
    ProjectFileV1 v1;
    ProjectFileV2 v2;
    ProjectFileV3 v3;
};

static_assert(sizeof(ProjectFileV1) == 1807, "GBX v1 layout changed");
static_assert(sizeof(ProjectFileV2) == 3346, "GBX v2 layout changed");
static_assert(sizeof(ProjectFileV3) == 6226, "unexpected GBX v3 layout");

static bool readProjectFile(const char* path, ProjectBuffer& loaded, uint16_t& version) {
    File f = SD.open(path, FILE_READ);
    if (!f) return false;

    uint8_t head[8];
    if (f.read(head, sizeof(head)) != sizeof(head)) { f.close(); return false; }
    uint32_t magic;
    memcpy(&magic, head, sizeof(magic));
    memcpy(&version, head + sizeof(magic), sizeof(version));
    if (magic != GBX_MAGIC || (version != 1 && version != 2 && version != 3)) {
        f.close();
        return false;
    }

    f.seek(0);
    void* destination = version == 1 ? static_cast<void*>(&loaded.v1) :
                        version == 2 ? static_cast<void*>(&loaded.v2) :
                                       static_cast<void*>(&loaded.v3);
    const size_t expected = version == 1 ? sizeof(loaded.v1) :
                            version == 2 ? sizeof(loaded.v2) : sizeof(loaded.v3);
    const size_t got = f.read(static_cast<uint8_t*>(destination), expected);
    f.close();
    return got == expected;
}

static void slotPath(uint8_t slot, char* out, size_t n) {
    snprintf(out, n, "%s/P%u.gbx", DIR_PROJECTS, (unsigned)(slot + 1));
}

bool storageProjectExists(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return false;
    char path[64]; slotPath(slot, path, sizeof(path));
    if (SD.exists(path)) return true;
    char backupPath[72];
    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    return SD.exists(backupPath);
}

bool storageSaveProject(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return false;
    static ProjectFileV3 pf;   // static: too big for stack
    memset(&pf, 0, sizeof(pf));

    pf.magic = GBX_MAGIC;
    pf.version = GBX_VERSION;
    pf.bpm = g_bpm;
    pf.songLoopStart = g_songLoopStart;
    memcpy(pf.song, g_song, SONG_LENGTH);

    for (int s = 0; s < NUM_SYNTHS; s++) {
        SynthVoice& v = g_synths[s].v[0];
        pf.voices[s] = g_synths[s].voices;
        pf.synths[s] = { (uint8_t)v.oscMode, v.wtIndex,
                         v.fltCutoff, v.fltReso, v.fltEnvAmt,
                         v.ampDecRate, v.filtDecRate, v.volume };
    }
    for (int l = 0; l < NUM_DRUM_LANES; l++) {
        DrumLane& d = g_drumLanes[l];
        SaveDrumLane& o = pf.lanes[l];
        o.engine = d.engine; o.type = d.type; o.chokeGroup = d.chokeGroup;
        o.volume = d.volume; o.tune = d.tune; o.decay = d.decay;
        if (d.engine == ENG_SMPL && d.sampleSlot >= 0 && d.sampleSlot < g_numSamples)
            strncpy(o.sampleName, g_samples[d.sampleSlot].name, SAMPLE_NAME_LEN - 1);
    }
    for (int p = 0; p < NUM_PATTERNS; p++) {
        for (int s = 0; s < NUM_SYNTHS; s++)
            for (int st = 0; st < NUM_STEPS; st++) {
                const SynthCell& c = g_patterns[p].synth[s][st];
                SaveCell& o = pf.cells[p][s][st];
                for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                o.flags = (uint8_t)((c.accent ? 1 : 0) | (c.slide ? 2 : 0));
            }
        memcpy(pf.drums[p], g_patterns[p].drums, NUM_STEPS);
    }

    char path[64]; slotPath(slot, path, sizeof(path));
    char tempPath[72], backupPath[72];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    SD.remove(tempPath);
    File f = SD.open(tempPath, FILE_WRITE);
    if (!f) return false;
    size_t written = f.write((uint8_t*)&pf, sizeof(pf));
    f.flush();
    f.close();
    if (written != sizeof(pf)) { SD.remove(tempPath); return false; }

    SD.remove(backupPath);
    if (SD.exists(path) && !SD.rename(path, backupPath)) {
        SD.remove(tempPath);
        return false;
    }
    if (!SD.rename(tempPath, path)) {
        if (SD.exists(backupPath)) SD.rename(backupPath, path);
        return false;
    }
    return true;
}

// shared param/lane/song apply used by both format loaders
static void applySynth(int s, const SaveSynth& in, uint8_t voices) {
    SynthTrack& t = g_synths[s];
    t.forEach([&](SynthVoice& v) {
        v.oscMode     = static_cast<OscMode>(
            in.oscMode < OSC_COUNT ? in.oscMode : static_cast<uint8_t>(OSC_SAW));
        v.wtIndex     = (in.wtIndex < g_numWavetables) ? in.wtIndex : 0;
        v.fltCutoff   = in.cutoff;
        v.fltReso     = in.reso;
        v.fltEnvAmt   = in.envAmt;
        v.ampDecRate  = in.ampDec;
        v.filtDecRate = in.filtDec;
        v.volume      = in.volume;
        v.active      = false;
    });
    t.setVoices(voices);
}

static void applyLane(int l, const SaveDrumLane& o) {
    DrumLane& d = g_drumLanes[l];
    d.engine = static_cast<DrumEngine>(
        o.engine < ENG_COUNT ? o.engine : static_cast<uint8_t>(ENG_808));
    d.type   = o.type < DT_COUNT ? o.type : static_cast<uint8_t>(DT_KICK);
    d.chokeGroup = o.chokeGroup;
    d.volume = o.volume; d.tune = o.tune; d.decay = o.decay;
    d.sampleSlot = -1;
    d.sv.init(); d.smp.init();
    if (d.engine == ENG_SMPL && o.sampleName[0])
        d.sampleSlot = (int8_t)samplerLoad(o.sampleName);   // -1 if missing
}

bool storageLoadProject(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return false;
    char path[64]; slotPath(slot, path, sizeof(path));
    static ProjectBuffer loaded;
    uint16_t version = 0;
    if (!readProjectFile(path, loaded, version)) {
        // A truncated/corrupt primary is treated the same as a missing one.
        char backupPath[72];
        snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
        if (!readProjectFile(backupPath, loaded, version)) return false;
    }

    bool wasPlaying = g_playing;
    g_playing = false;   // pause audio triggering while we swap data
    bool ok = false;

    samplerClearAll();
    memset(g_patterns, 0, sizeof(g_patterns));
    memset(g_song, SONG_EMPTY, sizeof(g_song));

    if (version == 3) {
        const ProjectFileV3& pf = loaded.v3;
        g_bpm = pf.bpm;
        g_songLoopStart = pf.songLoopStart;
        memcpy(g_song, pf.song, SONG_LENGTH);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf.synths[s], pf.voices[s]);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf.lanes[l]);
        for (int p = 0; p < NUM_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < NUM_STEPS; st++) {
                    const SaveCell& c = pf.cells[p][s][st];
                    SynthCell& o = g_patterns[p].synth[s][st];
                    for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                    o.accent = c.flags & 1; o.slide = c.flags & 2;
                }
            memcpy(g_patterns[p].drums, pf.drums[p], NUM_STEPS);
        }
        ok = true;
    } else if (version == 2) {
        const ProjectFileV2& pf = loaded.v2;
        g_bpm = pf.bpm;
        g_songLoopStart = pf.songLoopStart;
        memcpy(g_song, pf.song, LEGACY_SONG_LENGTH);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf.synths[s], pf.voices[s]);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf.lanes[l]);
        for (int p = 0; p < LEGACY_NUM_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < NUM_STEPS; st++) {
                    const SaveCell& c = pf.cells[p][s][st];
                    SynthCell& o = g_patterns[p].synth[s][st];
                    for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                    o.accent = c.flags & 1; o.slide = c.flags & 2;
                }
            memcpy(g_patterns[p].drums, pf.drums[p], NUM_STEPS);
        }
        ok = true;
    } else {              // v1: legacy mono cells, migrate on load
        const ProjectFileV1& pf = loaded.v1;
        g_bpm = pf.bpm;
        g_songLoopStart = pf.songLoopStart;
        memcpy(g_song, pf.song, LEGACY_SONG_LENGTH);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf.synths[s], 1);
        for (int l = 0; l < NUM_DRUM_LANES; l++) applyLane(l, pf.lanes[l]);
        for (int p = 0; p < LEGACY_NUM_PATTERNS; p++) {
            for (int s = 0; s < NUM_SYNTHS; s++)
                for (int st = 0; st < NUM_STEPS; st++) {
                    const SaveCellV1& c = pf.cells[p][s][st];
                    g_patterns[p].synth[s][st].setMono(c.note, c.octave,
                                                       c.flags & 1, c.flags & 2);
                }
            memcpy(g_patterns[p].drums, pf.drums[p], NUM_STEPS);
        }
        ok = true;
    }

    if (g_bpm < 40 || g_bpm > 300) g_bpm = 128;
    if (g_songLoopStart >= SONG_LENGTH) g_songLoopStart = 0;
    for (int i = 0; i < SONG_LENGTH; ++i)
        if (g_song[i] != SONG_EMPTY && g_song[i] >= NUM_PATTERNS) g_song[i] = SONG_EMPTY;

    g_playing = wasPlaying && ok;
    return ok;
}
