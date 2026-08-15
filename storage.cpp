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
#include "synth_project.h"
#include "performance_project.h"
#include "audio_engine.h"
#include "project_publish.h"
#include <SD.h>
#include "sd_io_arbiter.h"
#include <memory>
#include <new>
#include <math.h>
#include <string.h>

uint8_t g_curProject = 0;

// A primary is considered known-good only after this boot successfully loads
// it or publishes it. If load had to fall back to .bak, the primary must not
// be rotated over that backup on the next save.
static uint8_t s_knownGoodPrimaryMask = 0;
static bool projectPrimaryKnownGood(uint8_t slot) {
    return slot < NUM_PROJECT_SLOTS && (s_knownGoodPrimaryMask & (1u << slot)) != 0;
}
static void projectSetPrimaryKnownGood(uint8_t slot, bool good) {
    if (slot >= NUM_PROJECT_SLOTS) return;
    const uint8_t bit = static_cast<uint8_t>(1u << slot);
    if (good) s_knownGoodPrimaryMask |= bit;
    else s_knownGoodPrimaryMask &= static_cast<uint8_t>(~bit);
}

#define GBX_MAGIC   0x31584247u   // "GBX1"
#define GBX_VERSION 9             // v9: HiChord/PO/MEDO performance state
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

struct __attribute__((packed)) SaveLoopMix {
    uint8_t volume[LOOP_STREAM_TRACKS];
    uint8_t mutedMask;
};

struct __attribute__((packed)) ProjectFileV7 {
    ProjectFileV6 base;
    SaveLoopMix loopMix;
};

struct __attribute__((packed)) ProjectFileV8 {
    ProjectFileV7 base;
    SaveSynthEngineState synthEngines[NUM_SYNTHS];
};

struct __attribute__((packed)) ProjectFileV9 {
    ProjectFileV8 base;
    SavePerformanceState performance;
};

union ProjectBuffer {
    ProjectFileV1 v1;
    ProjectFileV2 v2;
    ProjectFileV3 v3;
    ProjectFileV4 v4;
    ProjectFileV5 v5;
    ProjectFileV6 v6;
    ProjectFileV7 v7;
    ProjectFileV8 v8;
    ProjectFileV9 v9;
};

static_assert(sizeof(ProjectFileV1) == 1807, "GBX v1 layout changed");
static_assert(sizeof(ProjectFileV2) == 3346, "GBX v2 layout changed");
static_assert(sizeof(ProjectFileV3) == 6226, "unexpected GBX v3 layout");
static_assert(sizeof(SaveSamplerSlot) == 190, "sampler slot layout changed");
static_assert(sizeof(SaveSamplerLock) == 16, "sampler lock layout changed");
static_assert(sizeof(ProjectFileV4) == 15412, "unexpected GBX v4 layout");
static_assert(sizeof(ProjectFileV5) == 31808, "unexpected GBX v5 layout");
static_assert(sizeof(ProjectFileV6) == 31816, "unexpected GBX v6 layout");
static_assert(sizeof(ProjectFileV7) == 31823, "unexpected GBX v7 layout");
static_assert(sizeof(ProjectFileV8) == 32117, "unexpected GBX v8 layout");
static_assert(sizeof(ProjectFileV9) == 33805, "unexpected GBX v9 layout");

// Project I/O is serialized on the main task and forbidden while recorders or
// the streamed sampler own storage. Allocate the 31 KiB union only for the
// duration of save/load; samplerInit's boot reserve deliberately leaves room
// for this operation. Keeping it static would tax every audio frame forever.
using ProjectBufferPtr = std::unique_ptr<ProjectBuffer>;

static ProjectBufferPtr allocateProjectBuffer() {
    return ProjectBufferPtr(new (std::nothrow) ProjectBuffer);
}

static bool readChunked(File& file, uint8_t* destination, size_t bytes) {
    constexpr size_t kChunkBytes = 4096;
    size_t done = 0;
    while (done < bytes) {
        const size_t remaining = bytes - done;
        const size_t request = remaining < kChunkBytes ? remaining : kChunkBytes;
        int got = 0;
        { SdIoGuard guard; got = file.read(destination + done, request); }
        if (got <= 0 || static_cast<size_t>(got) > request) return false;
        done += static_cast<size_t>(got);
    }
    return true;
}

static bool writeChunked(File& file, const uint8_t* source, size_t bytes) {
    constexpr size_t kChunkBytes = 4096;
    size_t done = 0;
    while (done < bytes) {
        const size_t remaining = bytes - done;
        const size_t request = remaining < kChunkBytes ? remaining : kChunkBytes;
        size_t written = 0;
        { SdIoGuard guard; written = file.write(source + done, request); }
        if (written == 0 || written > request) return false;
        done += written;
    }
    return true;
}

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
                        version == 6 ? static_cast<void*>(&loaded.v6) :
                        version == 7 ? static_cast<void*>(&loaded.v7) :
                        version == 8 ? static_cast<void*>(&loaded.v8) :
                                       static_cast<void*>(&loaded.v9);
    const size_t expected = version == 1 ? sizeof(loaded.v1) :
                            version == 2 ? sizeof(loaded.v2) :
                            version == 3 ? sizeof(loaded.v3) :
                            version == 4 ? sizeof(loaded.v4) :
                            version == 5 ? sizeof(loaded.v5) :
                            version == 6 ? sizeof(loaded.v6) :
                            version == 7 ? sizeof(loaded.v7) :
                            version == 8 ? sizeof(loaded.v8) : sizeof(loaded.v9);
    const bool readOk = readChunked(f, static_cast<uint8_t*>(destination), expected);
    { SdIoGuard guard; f.close(); }
    return readOk;
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
    ProjectBufferPtr buffer = allocateProjectBuffer();
    if (!buffer) return false;
    ProjectFileV9& pf = buffer->v9;
    memset(&pf, 0, sizeof(pf));

    ProjectFileV8& v8 = pf.base;
    ProjectFileV7& v7 = v8.base;
    ProjectFileV6& v6 = v7.base;
    ProjectFileV5& v5 = v6.base;
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
        synthProjectEncode(g_synths[s], v8.synthEngines[s]);
    }
    for (int l = 0; l < NUM_DRUM_LANES; l++) {
        DrumLane& d = g_drumLanes[l];
        SaveDrumLane& o = base.lanes[l];
        o.engine = d.engine; o.type = d.type; o.chokeGroup = d.chokeGroup;
        o.volume = d.volume; o.tune = d.tune; o.decay = d.decay;
        if (d.engine == ENG_SMPL) {
            const char* sampleName = samplerReferenceName(d.sampleSlot);
            if (sampleName[0])
                strncpy(o.sampleName, sampleName, SAMPLE_NAME_LEN - 1);
        }
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
        v6.motionMappings[mapping] = motion.mappings[mapping];
    const LoopEngineSnapshot loops = loopEngineSnapshot();
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        const LoopStreamTrackSnapshot& item = loops.tracks[track];
        v7.loopMix.volume[track] = static_cast<uint8_t>(
            (static_cast<uint32_t>(item.volumeQ15) * 100u + 16383u) / 32767u);
        if (item.state == LOOP_STREAM_MUTED)
            v7.loopMix.mutedMask |= static_cast<uint8_t>(1u << track);
    }
    performanceProjectEncode(pf.performance);

    char path[64]; slotPath(slot, path, sizeof(path));
    char tempPath[72], backupPath[72], previousBackupPath[80];
    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);
    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
    snprintf(previousBackupPath, sizeof(previousBackupPath), "%s.bak.prev", path);
    File f;
    { SdIoGuard guard; SD.remove(tempPath); f = SD.open(tempPath, FILE_WRITE); }
    if (!f) return false;
    const bool writeOk = writeChunked(f, reinterpret_cast<const uint8_t*>(&pf), sizeof(pf));
    { SdIoGuard guard; f.flush(); f.close(); }
    if (!writeOk) { SdIoGuard guard; SD.remove(tempPath); return false; }

    bool published = false;
    {
        SdIoGuard guard;
        published = projectPublishTempFile(
            SD, path, tempPath, backupPath, previousBackupPath,
            projectPrimaryKnownGood(slot));
        if (!published) SD.remove(tempPath);
    }
    if (published) projectSetPrimaryKnownGood(slot, true);
    return published;
}

// shared param/lane/song apply used by both format loaders
static void applySynth(int s, const SaveSynth& in, uint8_t voices) {
    SynthTrack& t = g_synths[s];
    // storageLoadProject holds the audio mutation gate here. Stop all legacy
    // and expanded voices before replacing a patch so same-engine loads cannot
    // leave a latched MGX/FM4 voice running with the new project's settings.
    t.hardStop();
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
    synthProjectMigrateLegacy(t);
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

static bool validateSynthRecord(const SaveSynth& synth) {
    return synth.oscMode < OSC_COUNT && isfinite(synth.cutoff) &&
           isfinite(synth.reso) && isfinite(synth.envAmt) &&
           isfinite(synth.ampDec) && isfinite(synth.filtDec) &&
           isfinite(synth.volume) && synth.cutoff >= 0.0f && synth.cutoff <= 1.0f &&
           synth.reso >= 0.0f && synth.reso <= 1.0f &&
           synth.envAmt >= 0.0f && synth.envAmt <= 1.0f &&
           synth.ampDec >= 0.9990f && synth.ampDec <= 0.99999f &&
           synth.filtDec >= 0.9950f && synth.filtDec <= 0.99995f &&
           synth.volume >= 0.0f && synth.volume <= 1.0f;
}

static bool validateLaneRecord(const SaveDrumLane& lane) {
    return lane.engine < ENG_COUNT && lane.type < DT_COUNT &&
           lane.chokeGroup < 4 && lane.sampleName[SAMPLE_NAME_LEN - 1] == 0 &&
           isfinite(lane.volume) && lane.volume >= 0.0f && lane.volume <= 1.0f &&
           isfinite(lane.tune) && lane.tune >= -12.0f && lane.tune <= 12.0f &&
           isfinite(lane.decay) && lane.decay >= 0.4f && lane.decay <= 2.5f;
}

static bool validateCellRecord(const SaveCell& cell) {
    if ((cell.flags & ~3u) != 0) return false;
    bool sawEmpty = false;
    for (uint8_t voice = 0; voice < MAX_POLY; ++voice) {
        if (cell.note[voice] == NOTE_EMPTY) { sawEmpty = true; continue; }
        if (sawEmpty || cell.note[voice] > 12 || cell.oct[voice] < 1 ||
            cell.oct[voice] > 7) return false;
    }
    return true;
}

static bool validateBaseV3(const ProjectFileV3& pf) {
    if (pf.bpm < 40 || pf.bpm > 300 || pf.songLoopStart >= SONG_LENGTH) return false;
    for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
        if (pf.voices[synth] < 1 || pf.voices[synth] > MAX_POLY ||
            !validateSynthRecord(pf.synths[synth])) return false;
    for (uint8_t lane = 0; lane < NUM_DRUM_LANES; ++lane)
        if (!validateLaneRecord(pf.lanes[lane])) return false;
    for (uint16_t index = 0; index < SONG_LENGTH; ++index)
        if (pf.song[index] != SONG_EMPTY && pf.song[index] >= NUM_PATTERNS) return false;
    for (uint8_t pattern = 0; pattern < NUM_PATTERNS; ++pattern)
        for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
            for (uint8_t step = 0; step < NUM_STEPS; ++step)
                if (!validateCellRecord(pf.cells[pattern][synth][step])) return false;
    return true;
}

static bool validateBaseV2(const ProjectFileV2& pf) {
    if (pf.bpm < 40 || pf.bpm > 300 ||
        pf.songLoopStart >= LEGACY_SONG_LENGTH) return false;
    for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
        if (pf.voices[synth] < 1 || pf.voices[synth] > MAX_POLY ||
            !validateSynthRecord(pf.synths[synth])) return false;
    for (uint8_t lane = 0; lane < NUM_DRUM_LANES; ++lane)
        if (!validateLaneRecord(pf.lanes[lane])) return false;
    for (uint8_t index = 0; index < LEGACY_SONG_LENGTH; ++index)
        if (pf.song[index] != SONG_EMPTY && pf.song[index] >= LEGACY_NUM_PATTERNS)
            return false;
    for (uint8_t pattern = 0; pattern < LEGACY_NUM_PATTERNS; ++pattern)
        for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
            for (uint8_t step = 0; step < NUM_STEPS; ++step)
                if (!validateCellRecord(pf.cells[pattern][synth][step])) return false;
    return true;
}

static bool validateBaseV1(const ProjectFileV1& pf) {
    if (pf.bpm < 40 || pf.bpm > 300 ||
        pf.songLoopStart >= LEGACY_SONG_LENGTH) return false;
    for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
        if (!validateSynthRecord(pf.synths[synth])) return false;
    for (uint8_t lane = 0; lane < NUM_DRUM_LANES; ++lane)
        if (!validateLaneRecord(pf.lanes[lane])) return false;
    for (uint8_t index = 0; index < LEGACY_SONG_LENGTH; ++index)
        if (pf.song[index] != SONG_EMPTY && pf.song[index] >= LEGACY_NUM_PATTERNS)
            return false;
    for (uint8_t pattern = 0; pattern < LEGACY_NUM_PATTERNS; ++pattern)
        for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
            for (uint8_t step = 0; step < NUM_STEPS; ++step) {
                const SaveCellV1& cell = pf.cells[pattern][synth][step];
                if ((cell.flags & ~3u) != 0 ||
                    (cell.note != NOTE_EMPTY &&
                     (cell.note > 12 || cell.octave < 1 || cell.octave > 7)))
                    return false;
            }
    return true;
}

struct SamplerStage {
    SamplerSlotBank bank;
    SamplerSequence sequence;
};

static bool stageSamplerV4(const ProjectFileV4& pf, SamplerStage& staged) {
    SamplerSlotBank& bank = staged.bank;
    SamplerSequence& sequence = staged.sequence;
    bank.clear();
    sequence.clear();
    for (uint8_t index = 0; index < SAMPLER_SLOT_COUNT; ++index) {
        const SaveSamplerSlot& in = pf.samplerSlots[index];
        if (in.mode == SAMPLER_SLOT_EMPTY) continue;
        const bool safeName = in.filename[0] != 0 &&
            in.filename[SAMPLE_NAME_LEN - 1] == 0 &&
            strchr(in.filename, '/') == nullptr && strchr(in.filename, '\\') == nullptr;
        if ((in.mode != SAMPLER_SLOT_MELODIC && in.mode != SAMPLER_SLOT_SLICED) ||
            !safeName || in.rootMidi > 127 || in.pitchQ8 < -24 * 256 ||
            in.pitchQ8 > 24 * 256 || in.gainQ15 > 32767 ||
            in.cutoffQ15 > 32767 || in.resonanceQ15 > 32767 ||
            !bank.assign(index, in.filename, in.sourceFrames, in.sourceRate,
                         static_cast<SamplerSlotMode>(in.mode)) ||
            bank.slot(index).quotaFrames != in.quotaFrames ||
            !bank.setTrim(index, in.trimStart, in.trimLength))
            return false;
        SamplerSlot& out = bank.slot(index);
        bank.beginEdit(index);  // direct-field writes need the seqlock (P3)
        out.rootMidi = in.rootMidi;
        out.pitchQ8 = in.pitchQ8;
        out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15;
        out.resonanceQ15 = in.resonanceQ15;
        bank.endEdit(index);
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
        if ((in.flags & ~knownFlags) != 0 || in.pitchQ8 < -24 * 256 ||
            in.pitchQ8 > 24 * 256 || in.gainQ15 > 32767 ||
            in.cutoffQ15 > 32767 || in.resonanceQ15 > 32767) return false;
        SamplerLockEntry out = {};
        out.pattern = in.pattern; out.step = in.step; out.slot = in.slot; out.flags = in.flags;
        out.pitchQ8 = in.pitchQ8; out.gainQ15 = in.gainQ15;
        out.cutoffQ15 = in.cutoffQ15; out.resonanceQ15 = in.resonanceQ15;
        out.trimStartQ15 = in.trimStartQ15; out.trimLengthQ15 = in.trimLengthQ15;
        if (!sequence.setLock(out)) return false;
    }
    if (!sequence.validate()) return false;
    return true;
}

static bool validateEventsV5(const ProjectFileV5& pf, bool highResolution) {
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
            event.type < EVENT_LOOP_NOTE || event.type > EVENT_LOOP_CONTROL ||
            (event.type == EVENT_LOOP_CONTROL
                 ? event.flags != 0
                 : event.type == EVENT_LOOP_NOTE
                     ? (event.flags & ~(EVENT_LOOP_FLAG_NOTE_OFF |
                                        EVENT_LOOP_FLAG_ROLE_GAIN)) != 0
                     : (event.flags & ~EVENT_LOOP_FLAG_ROLE_GAIN) != 0))
            return false;
        const uint16_t bars = pf.eventTracks[event.track].bars == 0
            ? 128 : pf.eventTracks[event.track].bars;
        const uint16_t units = highResolution ? EVENT_LOOP_TICKS_PER_BAR
                                              : EVENT_LOOP_LEGACY_STEPS_PER_BAR;
        if (event.step >= bars * units) return false;
        if ((event.type == EVENT_LOOP_NOTE &&
             (event.target >= NUM_SYNTHS || event.value1 < 12 || event.value1 > 127 ||
              event.value2 > 127)) ||
            (event.type == EVENT_LOOP_DRUM &&
             (event.target >= NUM_DRUM_LANES || event.value1 > 127)) ||
            (event.type == EVENT_LOOP_SAMPLE &&
             (event.target >= SAMPLER_SLOT_COUNT || event.value1 >= SAMPLER_SLICE_COUNT ||
              event.value2 > 127)) ||
            (event.type == EVENT_LOOP_CONTROL &&
             (event.target <= MOTION_TARGET_NONE || event.target >= MOTION_TARGET_COUNT ||
              event.value1 > 127)))
            return false;
    }

    return true;
}

static bool applyEventsV5(const ProjectFileV5& pf, bool highResolution) {
    if (!validateEventsV5(pf, highResolution)) return false;
    g_eventLooper.clearAll();
    for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
        const uint16_t bars = pf.eventTracks[track].bars == 0
            ? 128 : pf.eventTracks[track].bars;
        g_eventLooper.setBars(track, bars);
        g_eventLooper.setArmed(track, (pf.eventTracks[track].flags & 1u) != 0);
        g_eventLooper.setMuted(track, (pf.eventTracks[track].flags & 2u) != 0);
    }
    for (uint16_t index = 0; index < pf.eventCount; ++index) {
        EventLoopEvent event = pf.events[index];
        if (!highResolution) event.step = static_cast<uint16_t>(
            event.step * EVENT_LOOP_TICKS_PER_STEP);
        if (!g_eventLooper.appendLoaded(event)) return false;
    }
    return true;
}

static bool validateMotionV6(const ProjectFileV6& pf) {
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const MotionMapping& item = pf.motionMappings[mapping];
        const bool empty = item.source == MOTION_SOURCE_NONE &&
                           item.target == MOTION_TARGET_NONE;
        if (!empty && (item.source <= MOTION_SOURCE_NONE ||
                       item.source >= MOTION_SOURCE_COUNT ||
                       item.target <= MOTION_TARGET_NONE ||
                       item.target >= MOTION_TARGET_COUNT)) return false;
    }
    return true;
}

static bool applyMotionV6(const ProjectFileV6& pf) {
    if (!validateMotionV6(pf)) return false;
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const MotionMapping& item = pf.motionMappings[mapping];
        if (item.source == MOTION_SOURCE_NONE) motionClearMapping(mapping);
        else motionSetMapping(mapping, static_cast<MotionSource>(item.source),
                              static_cast<MotionTarget>(item.target));
    }
    return true;
}

static bool validateLoopMixV7(const SaveLoopMix& mix) {
    if ((mix.mutedMask & ~((1u << LOOP_STREAM_TRACKS) - 1u)) != 0) return false;
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track)
        if (mix.volume[track] > 100) return false;
    return true;
}

static bool applyLoopMixV7(const SaveLoopMix& mix) {
    if (!validateLoopMixV7(mix)) return false;
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        loopEngineSetVolume(track, mix.volume[track]);
        const LoopStreamState state = loopEngineSnapshot().tracks[track].state;
        const bool wantMuted = (mix.mutedMask & (1u << track)) != 0;
        if (wantMuted && state == LOOP_STREAM_PLAYING) loopEngineSetMuted(track, true);
        if (!wantMuted && state == LOOP_STREAM_MUTED) loopEngineSetMuted(track, false);
    }
    return true;
}

static void resetLoopMix() {
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        loopEngineSetVolume(track, 100);
        if (loopEngineSnapshot().tracks[track].state == LOOP_STREAM_MUTED)
            loopEngineSetMuted(track, false);
    }
}

static bool stageProject(const ProjectBuffer& loaded, uint16_t version,
                         std::unique_ptr<SamplerStage>& samplerStage) {
    samplerStage.reset();
    if (version >= 3) {
        const ProjectFileV3& baseFile = version == 9 ? loaded.v9.base.base.base.base.base.base :
                                             version == 8 ? loaded.v8.base.base.base.base.base :
                                             version == 7 ? loaded.v7.base.base.base.base :
                                             version == 6 ? loaded.v6.base.base.base :
                                             version == 5 ? loaded.v5.base.base :
                                             version == 4 ? loaded.v4.base : loaded.v3;
        if (!validateBaseV3(baseFile)) return false;
    } else if (version == 2) {
        if (!validateBaseV2(loaded.v2)) return false;
    } else if (version == 1) {
        if (!validateBaseV1(loaded.v1)) return false;
    } else return false;

    if (version >= 4) {
        samplerStage.reset(new (std::nothrow) SamplerStage);
        if (!samplerStage) return false;
        const ProjectFileV4& samplerFile =
            version == 9 ? loaded.v9.base.base.base.base.base :
            version == 8 ? loaded.v8.base.base.base.base :
            version == 7 ? loaded.v7.base.base.base :
            version == 6 ? loaded.v6.base.base :
            version == 5 ? loaded.v5.base : loaded.v4;
        if (!stageSamplerV4(samplerFile, *samplerStage)) return false;
    }
    if (version >= 5) {
        const ProjectFileV5& eventFile = version == 9 ? loaded.v9.base.base.base.base :
                                                version == 8 ? loaded.v8.base.base.base :
                                                version == 7 ? loaded.v7.base.base :
                                                version == 6 ? loaded.v6.base : loaded.v5;
        if (!validateEventsV5(eventFile, version == 9)) return false;
    }
    if (version >= 6) {
        const ProjectFileV6& motionFile = version == 9 ? loaded.v9.base.base.base :
                                                 version == 8 ? loaded.v8.base.base :
                                                 version == 7 ? loaded.v7.base : loaded.v6;
        if (!validateMotionV6(motionFile)) return false;
    }
    if (version >= 7) {
        const SaveLoopMix& mix = version == 9 ? loaded.v9.base.base.loopMix :
                                 version == 8 ? loaded.v8.base.loopMix : loaded.v7.loopMix;
        if (!validateLoopMixV7(mix)) return false;
    }
    if (version >= 8)
        for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth)
            if (!synthProjectValidate(version == 9 ? loaded.v9.base.synthEngines[synth] :
                                                   loaded.v8.synthEngines[synth])) return false;
    if (version == 9 && !performanceProjectValidate(loaded.v9.performance)) return false;
    return true;
}

bool storageLoadProject(uint8_t slot) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy() || loopEngineIsRecording() ||
        streamingSamplerBusy()) return false;
    char path[64]; slotPath(slot, path, sizeof(path));
    ProjectBufferPtr buffer = allocateProjectBuffer();
    if (!buffer) return false;
    ProjectBuffer& loaded = *buffer;
    uint16_t version = 0;
    std::unique_ptr<SamplerStage> samplerStage;
    bool loadedFromBackup = false;
    if (!readProjectFile(path, loaded, version) ||
        !stageProject(loaded, version, samplerStage)) {
        // Missing, truncated, structurally corrupt, and semantically corrupt
        // primaries all fall back to the last atomically published project.
        projectSetPrimaryKnownGood(slot, false);
        loadedFromBackup = true;
        char backupPath[72];
        snprintf(backupPath, sizeof(backupPath), "%s.bak", path);
        if (!readProjectFile(backupPath, loaded, version) ||
            !stageProject(loaded, version, samplerStage)) return false;
    }

    bool wasPlaying = g_playing;
    g_playing = false;   // pause sequencer triggering while we swap data
    // The audio task still renders while g_playing is false. Acquire an
    // acknowledged block-boundary gate before touching SynthTrack patches,
    // voices, master-effects delay state, or vocoder filter state.
    if (!audioEngineBeginExclusiveMutation(500)) {
        g_playing = wasPlaying;
        return false;
    }
    bool ok = false;

    samplerClearAll();
    g_samplerSlotBank.clear();
    g_samplerSequence.clear();
    g_eventLooper.clearAll();
    motionResetMappings();
    resetLoopMix();
    performanceStateInit();
    memset(g_patterns, 0, sizeof(g_patterns));
    memset(g_song, SONG_EMPTY, sizeof(g_song));

    if (version >= 3 && version <= 9) {
        const ProjectFileV3& pf = version == 9 ? loaded.v9.base.base.base.base.base.base :
                                  version == 8 ? loaded.v8.base.base.base.base.base :
                                  version == 7 ? loaded.v7.base.base.base.base :
                                  version == 6 ? loaded.v6.base.base.base :
                                  version == 5 ? loaded.v5.base.base :
                                  version == 4 ? loaded.v4.base : loaded.v3;
        g_bpm = pf.bpm;
        g_songLoopStart = pf.songLoopStart;
        memcpy(g_song, pf.song, SONG_LENGTH);
        for (int s = 0; s < NUM_SYNTHS; s++) applySynth(s, pf.synths[s], pf.voices[s]);
        // Restore the streamed bank before legacy lane names.  If the adaptive
        // RAM pool is unavailable, samplerLoad can reuse a saved streamed slot.
        if (version >= 4) {
            g_samplerSlotBank = samplerStage->bank;
            g_samplerSequence = samplerStage->sequence;
        }
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
        if (version >= 5) {
            const ProjectFileV5& eventFile = version == 9 ? loaded.v9.base.base.base.base :
                                                    version == 8 ? loaded.v8.base.base.base :
                                                    version == 7 ? loaded.v7.base.base :
                                                    version == 6 ? loaded.v6.base : loaded.v5;
            ok = applyEventsV5(eventFile, version == 9);
        }
        if (ok && version >= 6) {
            const ProjectFileV6& motionFile = version == 9 ? loaded.v9.base.base.base :
                                                     version == 8 ? loaded.v8.base.base :
                                                     version == 7 ? loaded.v7.base : loaded.v6;
            ok = applyMotionV6(motionFile);
        }
        if (ok && version >= 7)
            ok = applyLoopMixV7(version == 9 ? loaded.v9.base.base.loopMix :
                                version == 8 ? loaded.v8.base.loopMix : loaded.v7.loopMix);
        if (ok && version >= 8)
            for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth) {
                const SaveSynthEngineState &savedSynth = version == 9
                    ? loaded.v9.base.synthEngines[synth] : loaded.v8.synthEngines[synth];
                if (!synthProjectDecode(savedSynth, g_synths[synth])) {
                    ok = false;
                    break;
                }
                // P3 (reconciliation report): the save format validates the
                // wavetable index against NUM_WT_TOTAL, but only
                // g_numWavetables tables are actually loaded on this boot; an
                // out-of-range index would select a zeroed (silent) table.
                // Clamp to 0 exactly like the legacy per-voice path does.
                if (g_synths[synth].mgxPatch.wavetable >= g_numWavetables)
                    g_synths[synth].mgxPatch.wavetable = 0;
            }
        if (ok && version == 9) ok = performanceProjectDecode(loaded.v9.performance);
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

    audioEngineEndExclusiveMutation();
    if (ok) projectSetPrimaryKnownGood(slot, !loadedFromBackup);
    g_playing = wasPlaying && ok;
    return ok;
}
