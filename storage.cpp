// ============================================================
// CardputerGroovebox - storage.cpp
// ============================================================
#include "storage.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "sampler_slots.h"
#include "streaming_sampler.h"
#include "event_looper.h"
#include "motion.h"
#include <SD.h>
#include "sd_io_arbiter.h"
#include <string.h>

uint8_t g_curProject = 0;

#define GBX_MAGIC   0x31584247u   // "GBX1"
#define GBX_VERSION 6             // v6: persistent motion mappings
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

struct __attribute__((packed)) SaveSamplerRegion {
    uint32_t startFrame;
    uint32_t lengthFrames;
};

struct __attribute__((packed)) SaveSamplerSlot {
    uint8_t mode;
    char filename[SAMPLE_NAME_LEN];
    uint32_t sourceFrames;
    uint32_t sourceRate;
    uint32_t quotaFrames;
    uint32_t trimStart;
    uint32_t trimLength;
    uint8_t rootMidi;
    int16_t pitchQ8;
    uint16_t gainQ15;
    uint16_t cutoffQ15;
    uint16_t resonanceQ15;
    SaveSamplerRegion slices[SAMPLER_SLICE_COUNT];
};

struct __attribute__((packed)) SaveSamplerLock {
    uint8_t pattern, step, slot, flags;
    int16_t pitchQ8;
    uint16_t gainQ15, cutoffQ15, resonanceQ15, trimStartQ15, trimLengthQ15;
};

struct __attribute__((packed)) ProjectFileV4 {
    ProjectFileV3 base;
    SaveSamplerSlot samplerSlots[SAMPLER_SLOT_COUNT];
    uint8_t samplerKeys[NUM_PATTERNS][NUM_STEPS][SAMPLER_SLOT_COUNT];
    uint16_t samplerLockCount;
    SaveSamplerLock samplerLocks[SAMPLER_LOCK_CAPACITY];
};

struct __attribute__((packed)) SaveEventTrack {
    uint8_t bars;                  // 0 encodes 128
    uint8_t flags;                 // bit0 armed, bit1 muted
};

struct __attribute__((packed)) ProjectFileV5 {
    ProjectFileV4 base;
    SaveEventTrack eventTracks[EVENT_LOOP_TRACKS];
    uint16_t eventCount;
    EventLoopEvent events[EVENT_LOOP_CAPACITY];
};

struct __attribute__((packed)) ProjectFileV6 {
    ProjectFileV5 base;
    MotionMapping motionMappings[MOTION_MAPPING_COUNT];
};

union ProjectBuffer {
    ProjectFileV1 v1;
    ProjectFileV2 v2;
    ProjectFileV3 v3;
    ProjectFileV4 v4;
    ProjectFileV5 v5;
    ProjectFileV6 v6;
};

static_assert(sizeof(ProjectFileV1) == 1807, "GBX v1 layout changed");
static_assert(sizeof(ProjectFileV2) == 3346, "GBX v2 layout changed");
static_assert(sizeof(ProjectFileV3) == 6226, "unexpected GBX v3 layout");
static_assert(sizeof(SaveSamplerSlot) == 190, "sampler slot layout changed");
static_assert(sizeof(SaveSamplerLock) == 16, "sampler lock layout changed");
static_assert(sizeof(ProjectFileV4) == 15412, "unexpected GBX v4 layout");
static_assert(sizeof(ProjectFileV5) == 31808, "unexpected GBX v5 layout");
static_assert(sizeof(ProjectFileV6) == 31816, "unexpected GBX v6 layout");

// Project I/O is serialized on the main task. Reusing one static buffer avoids
// retaining separate 31 KiB save and load copies in the S3's limited SRAM.
static ProjectBuffer s_projectBuffer;

static bool readProjectFile(const char* path, ProjectBuffer& loaded, uint16_t& version) {
    File f;
    { SdIoGuard guard; f = SD.open(path, FILE_READ); }
    if (!f) return false;

    uint8_t head[8];
    { SdIoGuard guard;
      if (f.read(head, sizeof(head)) != sizeof(head)) { f.close(); return false; } }
    uint32_t magic;
    memcpy(&magic, head, sizeof(magic));
    memcpy(&version, head + sizeof(magic), sizeof(version));
    if (magic != GBX_MAGIC || (version < 1 || version > GBX_VERSION)) {
        { SdIoGuard guard; f.close(); }
        return false;
    }

    { SdIoGuard guard; f.seek(0); }
    void* destination = version == 1 ? static_cast<void*>(&loaded.v1) :
                        version == 2 ? static_cast<void*>(&loaded.v2) :
                        version == 3 ? static_cast<void*>(&loaded.v3) :
                        version == 4 ? static_cast<void*>(&loaded.v4) :
                        version == 5 ? static_cast<void*>(&loaded.v5) :
                                       static_cast<void*>(&loaded.v6);
    const size_t expected = version == 1 ? sizeof(loaded.v1) :
                            version == 2 ? sizeof(loaded.v2) :
                            version == 3 ? sizeof(loaded.v3) :
                            version == 4 ? sizeof(loaded.v4) :
                            version == 5 ? sizeof(loaded.v5) : sizeof(loaded.v6);
    size_t got = 0;
    { SdIoGuard guard;
      got = f.read(static_cast<uint8_t*>(destination), expected);
      f.close(); }
    return got == expected;
}

static void slotPath(uint8_t slot, char* out, size_t n) {
    snprintf(out, n, "%s/P%u.gbx", DIR_PROJECTS, (unsigned)(slot + 1));
}

bool storageProjectExists(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy() || loopEngineIsRecording() ||
        streamingSamplerBusy()) return false;
    char path[64]; slotPath(slot, path, sizeof(path));
    { SdIoGuard guard; if (SD.exists(path)) return true; }
    char backupPath[72];
    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    { SdIoGuard guard; return SD.exists(backupPath); }
}

bool storageSaveProject(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy() || loopEngineIsRecording() ||
        streamingSamplerBusy()) return false;
    ProjectFileV6& pf = s_projectBuffer.v6;
    memset(&pf, 0, sizeof(pf));

    ProjectFileV5& v5 = pf.base;
    ProjectFileV4& v4 = v5.base;
    ProjectFileV3& base = v4.base;

    base.magic = GBX_MAGIC;
    base.version = GBX_VERSION;
    base.bpm = g_bpm;
    base.songLoopStart = g_songLoopStart;
    memcpy(base.song, g_song, SONG_LENGTH);

    for (int s = 0; s < NUM_SYNTHS; s++) {
        SynthVoice& v = g_synths[s].v[0];
        base.voices[s] = g_synths[s].voices;
        base.synths[s] = { (uint8_t)v.oscMode, v.wtIndex,
                         v.fltCutoff, v.fltReso, v.fltEnvAmt,
                         v.ampDecRate, v.filtDecRate, v.volume };
    }
    for (int l = 0; l < NUM_DRUM_LANES; l++) {
        DrumLane& d = g_drumLanes[l];
        SaveDrumLane& o = base.lanes[l];
        o.engine = d.engine; o.type = d.type; o.chokeGroup = d.chokeGroup;
        o.volume = d.volume; o.tune = d.tune; o.decay = d.decay;
        if (d.engine == ENG_SMPL && d.sampleSlot >= 0 && d.sampleSlot < g_numSamples)
            strncpy(o.sampleName, g_samples[d.sampleSlot].name, SAMPLE_NAME_LEN - 1);
    }
    for (int p = 0; p < NUM_PATTERNS; p++) {
        for (int s = 0; s < NUM_SYNTHS; s++)
            for (int st = 0; st < NUM_STEPS; st++) {
                const SynthCell& c = g_patterns[p].synth[s][st];
                SaveCell& o = base.cells[p][s][st];
                for (int i = 0; i < MAX_POLY; i++) { o.note[i] = c.note[i]; o.oct[i] = c.oct[i]; }
                o.flags = (uint8_t)((c.accent ? 1 : 0) | (c.slide ? 2 : 0));
            }
        memcpy(base.drums[p], g_patterns[p].drums, NUM_STEPS);
    }

    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        const SamplerSlot& in = g_samplerSlotBank.slot(slot);
        SaveSamplerSlot& out = v4.samplerSlots[slot];
        out.mode = in.mode;
        memcpy(out.filename, in.filename, sizeof(out.filename));
        out.sourceFrames = in.sourceFrames;
        out.sourceRate = in.sourceRate;
        out.quotaFrames = in.quotaFrames;
        out.trimStart = in.trimStart;
        out.trimLength = in.trimLength;
        out.rootMidi = in.rootMidi;
        out.pitchQ8 = in.pitchQ8;
        out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15;
        out.resonanceQ15 = in.resonanceQ15;
        for (uint8_t slice = 0; slice < SAMPLER_SLICE_COUNT; ++slice) {
            out.slices[slice].startFrame = in.slices[slice].startFrame;
            out.slices[slice].lengthFrames = in.slices[slice].lengthFrames;
        }
    }
    for (uint8_t pattern = 0; pattern < NUM_PATTERNS; ++pattern)
        for (uint8_t step = 0; step < NUM_STEPS; ++step)
            for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot)
                v4.samplerKeys[pattern][step][slot] =
                    g_samplerSequence.eventKey(pattern, step, slot);
    v4.samplerLockCount = g_samplerSequence.lockCount();
    for (uint16_t index = 0; index < v4.samplerLockCount; ++index) {
        const SamplerLockEntry& in = g_samplerSequence.lock(index);
        SaveSamplerLock& out = v4.samplerLocks[index];
        out.pattern = in.pattern; out.step = in.step; out.slot = in.slot; out.flags = in.flags;
        out.pitchQ8 = in.pitchQ8; out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15; out.resonanceQ15 = in.resonanceQ15;
        out.trimStartQ15 = in.trimStartQ15; out.trimLengthQ15 = in.trimLengthQ15;
    }

    for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
        const EventLoopTrackState& state = g_eventLooper.track(track);
        v5.eventTracks[track].bars = static_cast<uint8_t>(
            g_eventLooper.bars(track) == 128 ? 0 : g_eventLooper.bars(track));
        v5.eventTracks[track].flags = static_cast<uint8_t>(
            (state.armed ? 1u : 0u) | (state.muted ? 2u : 0u));
    }
    v5.eventCount = g_eventLooper.count();
    for (uint16_t index = 0; index < v5.eventCount; ++index)
        v5.events[index] = g_eventLooper.event(index);
    const MotionSnapshot motion = motionSnapshot();
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping)
        pf.motionMappings[mapping] = motion.mappings[mapping];

    char path[64]; slotPath(slot, path, sizeof(path));
    char tempPath[72], backupPath[72];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    File f;
    { SdIoGuard guard; SD.remove(tempPath); f = SD.open(tempPath, FILE_WRITE); }
    if (!f) return false;
    size_t written = 0;
    { SdIoGuard guard;
      written = f.write((uint8_t*)&pf, sizeof(pf));
      f.flush();
      f.close(); }
    if (written != sizeof(pf)) { SdIoGuard guard; SD.remove(tempPath); return false; }

    {
        SdIoGuard guard;
        SD.remove(backupPath);
        if (SD.exists(path) && !SD.rename(path, backupPath)) {
            SD.remove(tempPath);
            return false;
        }
        if (!SD.rename(tempPath, path)) {
            if (SD.exists(backupPath)) SD.rename(backupPath, path);
            return false;
        }
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

static bool applySamplerV4(const ProjectFileV4& pf) {
    SamplerSlotBank bank;
    SamplerSequence sequence;
    for (uint8_t index = 0; index < SAMPLER_SLOT_COUNT; ++index) {
        const SaveSamplerSlot& in = pf.samplerSlots[index];
        if (in.mode == SAMPLER_SLOT_EMPTY) continue;
        if ((in.mode != SAMPLER_SLOT_MELODIC && in.mode != SAMPLER_SLOT_SLICED) ||
            in.rootMidi > 127 || in.gainQ15 > 32767 || in.cutoffQ15 > 32767 ||
            in.resonanceQ15 > 32767 ||
            !bank.assign(index, in.filename, in.sourceFrames, in.sourceRate,
                         static_cast<SamplerSlotMode>(in.mode)) ||
            bank.slot(index).quotaFrames != in.quotaFrames ||
            !bank.setTrim(index, in.trimStart, in.trimLength))
            return false;
        SamplerSlot& out = bank.slot(index);
        out.rootMidi = in.rootMidi;
        out.pitchQ8 = in.pitchQ8;
        out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15;
        out.resonanceQ15 = in.resonanceQ15;
        for (uint8_t slice = 0; slice < SAMPLER_SLICE_COUNT; ++slice)
            if (!bank.setSlice(index, slice, in.slices[slice].startFrame,
                               in.slices[slice].lengthFrames))
                return false;
    }
    if (!bank.validate() || pf.samplerLockCount > SAMPLER_LOCK_CAPACITY) return false;
    for (uint8_t pattern = 0; pattern < NUM_PATTERNS; ++pattern)
        for (uint8_t step = 0; step < NUM_STEPS; ++step)
            for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
                const uint8_t key = pf.samplerKeys[pattern][step][slot];
                if (key != 0xFF && !sequence.setEvent(pattern, step, slot, key)) return false;
            }
    const uint8_t knownFlags = SAMPLER_LOCK_PITCH | SAMPLER_LOCK_GAIN |
                               SAMPLER_LOCK_FILTER | SAMPLER_LOCK_TRIM;
    for (uint16_t index = 0; index < pf.samplerLockCount; ++index) {
        const SaveSamplerLock& in = pf.samplerLocks[index];
        if ((in.flags & ~knownFlags) != 0 || in.gainQ15 > 32767 ||
            in.cutoffQ15 > 32767 || in.resonanceQ15 > 32767) return false;
        SamplerLockEntry out = {};
        out.pattern = in.pattern; out.step = in.step; out.slot = in.slot; out.flags = in.flags;
        out.pitchQ8 = in.pitchQ8; out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15; out.resonanceQ15 = in.resonanceQ15;
        out.trimStartQ15 = in.trimStartQ15; out.trimLengthQ15 = in.trimLengthQ15;
        if (!sequence.setLock(out)) return false;
    }
    if (!sequence.validate()) return false;
    g_samplerSlotBank = bank;
    g_samplerSequence = sequence;
    return true;
}

static bool applyEventsV5(const ProjectFileV5& pf) {
    if (pf.eventCount > EVENT_LOOP_CAPACITY) return false;
    for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
        const uint16_t bars = pf.eventTracks[track].bars == 0
            ? 128 : pf.eventTracks[track].bars;
        if (bars == 0 || bars > EVENT_LOOP_MAX_BARS ||
            (pf.eventTracks[track].flags & ~3u) != 0) return false;
    }
    for (uint16_t index = 0; index < pf.eventCount; ++index) {
        const EventLoopEvent& event = pf.events[index];
        if (event.track >= EVENT_LOOP_TRACKS ||
            event.type < EVENT_LOOP_NOTE || event.type > EVENT_LOOP_CONTROL)
            return false;
        const uint16_t bars = pf.eventTracks[event.track].bars == 0
            ? 128 : pf.eventTracks[event.track].bars;
        if (event.step >= bars * EVENT_LOOP_STEPS_PER_BAR) return false;
        if ((event.type == EVENT_LOOP_NOTE &&
             (event.target >= NUM_SYNTHS || event.value1 < 12 || event.value1 > 127)) ||
            (event.type == EVENT_LOOP_DRUM && event.target >= NUM_DRUM_LANES) ||
            (event.type == EVENT_LOOP_SAMPLE &&
             (event.target >= SAMPLER_SLOT_COUNT || event.value1 >= SAMPLER_SLICE_COUNT)))
            return false;
    }

    g_eventLooper.clearAll();
    for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
        const uint16_t bars = pf.eventTracks[track].bars == 0
            ? 128 : pf.eventTracks[track].bars;
        g_eventLooper.setBars(track, bars);
        g_eventLooper.setArmed(track, (pf.eventTracks[track].flags & 1u) != 0);
        g_eventLooper.setMuted(track, (pf.eventTracks[track].flags & 2u) != 0);
    }
    for (uint16_t index = 0; index < pf.eventCount; ++index)
        if (!g_eventLooper.appendLoaded(pf.events[index])) return false;
    return true;
}

static bool applyMotionV6(const ProjectFileV6& pf) {
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const MotionMapping& item = pf.motionMappings[mapping];
        const bool empty = item.source == MOTION_SOURCE_NONE &&
                           item.target == MOTION_TARGET_NONE;
        if (!empty && (item.source <= MOTION_SOURCE_NONE ||
                       item.source >= MOTION_SOURCE_COUNT ||
                       item.target <= MOTION_TARGET_NONE ||
                       item.target >= MOTION_TARGET_COUNT)) return false;
    }
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const MotionMapping& item = pf.motionMappings[mapping];
        if (item.source == MOTION_SOURCE_NONE) motionClearMapping(mapping);
        else motionSetMapping(mapping, static_cast<MotionSource>(item.source),
                              static_cast<MotionTarget>(item.target));
    }
    return true;
}

bool storageLoadProject(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy() || loopEngineIsRecording() ||
        streamingSamplerBusy()) return false;
    char path[64]; slotPath(slot, path, sizeof(path));
    ProjectBuffer& loaded = s_projectBuffer;
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
    g_samplerSlotBank.clear();
    g_samplerSequence.clear();
    g_eventLooper.clearAll();
    motionResetMappings();
    memset(g_patterns, 0, sizeof(g_patterns));
    memset(g_song, SONG_EMPTY, sizeof(g_song));

    if (version == 3 || version == 4 || version == 5 || version == 6) {
        const ProjectFileV3& pf = version == 6 ? loaded.v6.base.base.base :
                                  version == 5 ? loaded.v5.base.base :
                                  version == 4 ? loaded.v4.base : loaded.v3;
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
        ok = version == 6 ? (applySamplerV4(loaded.v6.base.base) &&
                             applyEventsV5(loaded.v6.base) && applyMotionV6(loaded.v6)) :
             version == 5 ? (applySamplerV4(loaded.v5.base) && applyEventsV5(loaded.v5)) :
             version == 4 ? applySamplerV4(loaded.v4) : true;
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
