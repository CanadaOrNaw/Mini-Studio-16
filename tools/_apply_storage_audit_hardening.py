#!/usr/bin/env python3
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    return (ROOT / path).read_text()


def write(path, text):
    target = ROOT / path
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)


def replace_once(path, old, new):
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected exactly one match, found {count}: {old[:80]!r}")
    write(path, text.replace(old, new, 1))


# ---------------------------------------------------------------------------
# Project publish transaction: preserve the last known-good backup even when
# the primary is corrupt or the final rename fails.
# ---------------------------------------------------------------------------
write("project_publish.h", r'''#pragma once

// Publish a fully-written temporary project file without sacrificing the last
// known-good backup. `primaryKnownGood` is deliberately conservative: it is
// true only after this boot successfully loaded or saved the primary.
//
// Power-loss-safe checkpoints:
// - a known-good previous backup is first moved to backupPrevious;
// - a known-good primary is then promoted to backup;
// - only then is temp published as primary;
// - on a failed final rename, the prior primary/backup arrangement is restored
//   where possible, and at least one known-good fallback remains reachable.
template <typename Fs>
bool projectPublishTempFile(Fs& fs, const char* primary, const char* temp,
                            const char* backup, const char* backupPrevious,
                            bool primaryKnownGood) {
    if (!fs.exists(temp)) return false;

    if (fs.exists(backupPrevious) && !fs.remove(backupPrevious)) return false;

    const bool primaryExists = fs.exists(primary);
    const bool backupExists = fs.exists(backup);
    bool oldBackupAside = false;
    bool primaryMovedToBackup = false;

    if (primaryExists) {
        if (primaryKnownGood) {
            if (backupExists) {
                if (!fs.rename(backup, backupPrevious)) return false;
                oldBackupAside = true;
            }
            if (!fs.rename(primary, backup)) {
                if (oldBackupAside) fs.rename(backupPrevious, backup);
                return false;
            }
            primaryMovedToBackup = true;
        } else if (backupExists) {
            // The primary failed validation/load during this boot. Preserve the
            // known-good backup instead of replacing it with corrupt bytes.
            if (!fs.remove(primary)) return false;
        } else {
            // No known-good backup exists. Keep the unknown primary as the only
            // available fallback while the new file is published.
            if (!fs.rename(primary, backup)) return false;
            primaryMovedToBackup = true;
        }
    }

    if (!fs.rename(temp, primary)) {
        bool restoredPrimary = true;
        if (primaryMovedToBackup && fs.exists(backup))
            restoredPrimary = fs.rename(backup, primary);
        // If restoring the newer primary failed, leave it reachable as .bak;
        // do not overwrite it with the older backupPrevious copy.
        if (oldBackupAside && restoredPrimary && !fs.exists(backup) &&
            fs.exists(backupPrevious))
            fs.rename(backupPrevious, backup);
        return false;
    }

    if (oldBackupAside && fs.exists(backupPrevious)) fs.remove(backupPrevious);
    return true;
}
''')

# ---------------------------------------------------------------------------
# Stateful host SD stub. The old stub returned success for every operation and
# could not execute a real project save/load transaction.
# ---------------------------------------------------------------------------
write("tests/stubs/SD.h", r'''#pragma once

#include "Arduino.h"
#include <algorithm>
#include <map>
#include <string>
#include <vector>

#define FILE_READ "r"
#define FILE_WRITE "w"

class SDStub;

class File {
public:
    File() : owner_(nullptr), position_(0), writable_(false), valid_(false) {}
    explicit operator bool() const;
    size_t write(const uint8_t* data, size_t length);
    int read(uint8_t* data, size_t length);
    bool seek(uint32_t position);
    int available() const;
    uint32_t position() const { return static_cast<uint32_t>(position_); }
    size_t size() const;
    const char* name() const { return path_.c_str(); }
    bool isDirectory() const { return false; }
    File openNextFile() { return File(); }
    void flush() {}
    void close() { valid_ = false; }

private:
    friend class SDStub;
    File(SDStub* owner, const char* path, bool writable)
        : owner_(owner), path_(path ? path : ""), position_(0),
          writable_(writable), valid_(true) {}

    SDStub* owner_;
    std::string path_;
    size_t position_;
    bool writable_;
    bool valid_;
};

class SDStub {
public:
    template <typename Spi>
    bool begin(uint8_t, Spi&, uint32_t) { return true; }

    bool exists(const char* path) const {
        return path && files_.find(path) != files_.end();
    }
    bool mkdir(const char*) { return true; }
    bool rmdir(const char*) { return true; }
    bool remove(const char* path) {
        if (!path) return false;
        return files_.erase(path) != 0;
    }
    bool rename(const char* from, const char* to) {
        if (!from || !to) return false;
        if (failRename_ && failRenameFrom_ == from && failRenameTo_ == to) {
            failRename_ = false;
            return false;
        }
        auto source = files_.find(from);
        if (source == files_.end() || files_.find(to) != files_.end()) return false;
        files_[to] = source->second;
        files_.erase(source);
        return true;
    }
    File open(const char* path, const char* mode) {
        if (!path || !mode) return File();
        const bool writable = mode[0] == 'w' || (mode[0] == 'r' && mode[1] == '+');
        if (mode[0] == 'w') files_[path].clear();
        else if (!exists(path)) return File();
        return File(this, path, writable);
    }
    File open(const char* path) { return open(path, FILE_READ); }

    void reset() {
        files_.clear();
        failRename_ = false;
        failRenameFrom_.clear();
        failRenameTo_.clear();
    }
    void put(const char* path, const uint8_t* data, size_t length) {
        std::vector<uint8_t>& file = files_[path ? path : ""];
        file.assign(data, data + length);
    }
    void put(const char* path, const std::vector<uint8_t>& data) {
        files_[path ? path : ""] = data;
    }
    const std::vector<uint8_t>* bytes(const char* path) const {
        auto it = files_.find(path ? path : "");
        return it == files_.end() ? nullptr : &it->second;
    }
    std::vector<uint8_t>* mutableBytes(const char* path) {
        auto it = files_.find(path ? path : "");
        return it == files_.end() ? nullptr : &it->second;
    }
    void failNextRename(const char* from, const char* to) {
        failRename_ = true;
        failRenameFrom_ = from ? from : "";
        failRenameTo_ = to ? to : "";
    }

private:
    friend class File;
    std::map<std::string, std::vector<uint8_t>> files_;
    bool failRename_ = false;
    std::string failRenameFrom_;
    std::string failRenameTo_;
};

inline File::operator bool() const {
    return valid_ && owner_ && owner_->exists(path_.c_str());
}
inline size_t File::write(const uint8_t* data, size_t length) {
    if (!valid_ || !owner_ || !writable_ || !data) return 0;
    std::vector<uint8_t>& file = owner_->files_[path_];
    if (position_ + length > file.size()) file.resize(position_ + length);
    std::copy(data, data + length, file.begin() + static_cast<std::ptrdiff_t>(position_));
    position_ += length;
    return length;
}
inline int File::read(uint8_t* data, size_t length) {
    if (!valid_ || !owner_ || !data) return 0;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end() || position_ >= it->second.size()) return 0;
    const size_t available = it->second.size() - position_;
    const size_t count = length < available ? length : available;
    std::copy(it->second.begin() + static_cast<std::ptrdiff_t>(position_),
              it->second.begin() + static_cast<std::ptrdiff_t>(position_ + count), data);
    position_ += count;
    return static_cast<int>(count);
}
inline bool File::seek(uint32_t position) {
    if (!valid_ || !owner_) return false;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end()) return false;
    if (position > it->second.size() && !writable_) return false;
    if (position > it->second.size()) it->second.resize(position);
    position_ = position;
    return true;
}
inline int File::available() const {
    if (!valid_ || !owner_) return 0;
    auto it = owner_->files_.find(path_);
    if (it == owner_->files_.end() || position_ >= it->second.size()) return 0;
    return static_cast<int>(it->second.size() - position_);
}
inline size_t File::size() const {
    if (!valid_ || !owner_) return 0;
    auto it = owner_->files_.find(path_);
    return it == owner_->files_.end() ? 0 : it->second.size();
}

extern SDStub SD;
''')

write("tests/test_project_publish.cpp", r'''#include "../project_publish.h"
#include <SD.h>
#include <assert.h>
#include <string.h>
#include <vector>

SDStub SD;

static std::vector<uint8_t> data(const char* text) {
    return std::vector<uint8_t>(text, text + strlen(text));
}
static void expect(const char* path, const char* text) {
    const std::vector<uint8_t>* bytes = SD.bytes(path);
    assert(bytes && *bytes == data(text));
}

int main() {
    const char* primary = "/P1.gbx";
    const char* temp = "/P1.gbx.tmp";
    const char* backup = "/P1.gbx.bak";
    const char* previous = "/P1.gbx.bak.prev";

    // Known-good primary rotates to backup; older backup retires only after
    // the new primary is safely published.
    SD.reset();
    SD.put(primary, data("primary-old"));
    SD.put(backup, data("backup-older"));
    SD.put(temp, data("primary-new"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, true));
    expect(primary, "primary-new");
    expect(backup, "primary-old");
    assert(!SD.exists(previous));

    // A primary that failed validation must never replace a known-good .bak.
    SD.reset();
    SD.put(primary, data("CORRUPT"));
    SD.put(backup, data("known-good"));
    SD.put(temp, data("new-good"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, false));
    expect(primary, "new-good");
    expect(backup, "known-good");

    // If publishing the temp fails, both known-good generations survive.
    SD.reset();
    SD.put(primary, data("primary-old"));
    SD.put(backup, data("backup-older"));
    SD.put(temp, data("primary-new"));
    SD.failNextRename(temp, primary);
    assert(!projectPublishTempFile(SD, primary, temp, backup, previous, true));
    expect(primary, "primary-old");
    expect(backup, "backup-older");

    // Backup-only recovery remains usable while a fresh primary is published.
    SD.reset();
    SD.put(backup, data("known-good"));
    SD.put(temp, data("primary-new"));
    assert(projectPublishTempFile(SD, primary, temp, backup, previous, false));
    expect(primary, "primary-new");
    expect(backup, "known-good");
    return 0;
}
''')

write("input_page_core.h", r'''#pragma once
#include "config.h"

static inline bool inputPageNeedsPerformanceStop(Page page) {
    return page == PAGE_CHORD || page == PAGE_MEDO;
}
''')

write("tests/test_input_page_core.cpp", r'''#include "../input_page_core.h"
#include <assert.h>

int main() {
    assert(inputPageNeedsPerformanceStop(PAGE_CHORD));
    assert(inputPageNeedsPerformanceStop(PAGE_MEDO));
    for (uint8_t page = 0; page < PAGE_COUNT; ++page) {
        if (page == PAGE_CHORD || page == PAGE_MEDO) continue;
        assert(!inputPageNeedsPerformanceStop(static_cast<Page>(page)));
    }
    return 0;
}
''')

# ---------------------------------------------------------------------------
# Production storage.cpp execution harness: real v1-v9 loader/save path,
# stateful in-memory SD, fallback/rollback, and audio mutation gate contract.
# ---------------------------------------------------------------------------
write("tests/test_storage_project.cpp", r'''#include "../storage.h"
#include "../sequencer.h"
#include "../sampler.h"
#include "../sampler_slots.h"
#include "../event_looper.h"
#include "../motion.h"
#include "../loop_engine.h"
#include "../performance_state.h"
#include "../audio_engine.h"
#include "../sd_io_arbiter.h"
#include <SD.h>
#include <assert.h>
#include <string.h>
#include <vector>

SDStub SD;

Pattern g_patterns[NUM_PATTERNS];
uint8_t g_song[SONG_LENGTH];
uint8_t g_songLoopStart = 0;
uint8_t g_patternBank = 0;
SynthTrack g_synths[NUM_SYNTHS];
DrumLane g_drumLanes[NUM_DRUM_LANES];
bool g_synthMute[NUM_SYNTHS] = {};
bool g_drumMute = false;
volatile bool g_playing = false;
bool g_recEnabled = false;
bool g_songMode = false;
uint8_t g_playStep = 0;
uint8_t g_playPattern = 0;
uint8_t g_songPos = 0;
uint16_t g_bpm = 128;
uint8_t g_swing = 50;
uint8_t g_curPattern = 0;
uint8_t g_curTrack = 0;
uint8_t g_curStep = 0;
uint8_t g_curDrumLane = 0;
uint8_t g_curOctave = 4;

int16_t* g_samplePool = nullptr;
uint32_t g_poolUsed = 0;
uint32_t g_poolCapacity = 0;
SampleInfo g_samples[MAX_SAMPLES] = {};
uint8_t g_numSamples = 0;
SampleVoice g_previewVoice;

EventLooperCore g_eventLooper;
uint16_t g_eventLoopPosition = 0;

static MotionSnapshot s_motion = {};
static LoopEngineSnapshot s_loops = {};
static uint32_t s_gateBegins = 0;
static uint32_t s_gateEnds = 0;
static bool s_gateAllowed = true;

bool masterRecorderIsBusy() { return false; }
bool stemRecorderIsBusy() { return false; }
bool loopEngineIsRecording() { return false; }
bool streamingSamplerBusy() { return false; }

void samplerClearAll() {
    g_poolUsed = 0;
    g_numSamples = 0;
    memset(g_samples, 0, sizeof(g_samples));
    g_previewVoice.init();
}
int samplerLoad(const char*) { return -1; }
const char* samplerReferenceName(int) { return ""; }
bool samplerReferenceIsStreamed(int) { return false; }
bool samplerTriggerStreamedReference(int, float, float) { return false; }
int8_t samplerReserveStreamReference() { return -1; }
int8_t samplerMakeStreamReference(uint8_t) { return -1; }
void samplerReleaseStreamReference(int8_t) {}
bool samplerDecodeStreamReference(int, uint8_t&) { return false; }

void motionResetMappings() {
    memset(&s_motion, 0, sizeof(s_motion));
}
bool motionSetMapping(uint8_t mapping, MotionSource source, MotionTarget target) {
    if (mapping >= MOTION_MAPPING_COUNT) return false;
    s_motion.mappings[mapping].source = source;
    s_motion.mappings[mapping].target = target;
    return true;
}
void motionClearMapping(uint8_t mapping) {
    if (mapping < MOTION_MAPPING_COUNT) s_motion.mappings[mapping] = {MOTION_SOURCE_NONE, MOTION_TARGET_NONE};
}
MotionSnapshot motionSnapshot() { return s_motion; }

LoopEngineSnapshot loopEngineSnapshot() { return s_loops; }
bool loopEngineSetVolume(uint8_t track, uint8_t percent) {
    if (track >= LOOP_STREAM_TRACKS || percent > 100) return false;
    s_loops.tracks[track].volumeQ15 = static_cast<int16_t>(percent * 32767u / 100u);
    return true;
}
bool loopEngineSetMuted(uint8_t track, bool muted) {
    if (track >= LOOP_STREAM_TRACKS) return false;
    if (muted && s_loops.tracks[track].state == LOOP_STREAM_PLAYING)
        s_loops.tracks[track].state = LOOP_STREAM_MUTED;
    else if (!muted && s_loops.tracks[track].state == LOOP_STREAM_MUTED)
        s_loops.tracks[track].state = LOOP_STREAM_PLAYING;
    return true;
}

bool audioEngineBeginExclusiveMutation(uint32_t) {
    ++s_gateBegins;
    return s_gateAllowed;
}
void audioEngineEndExclusiveMutation() { ++s_gateEnds; }

static void initState() {
    memset(g_patterns, 0, sizeof(g_patterns));
    memset(g_song, SONG_EMPTY, sizeof(g_song));
    g_songLoopStart = 0;
    g_bpm = 128;
    g_swing = 50;
    g_playing = false;
    g_samplerSlotBank.clear();
    g_samplerSequence.clear();
    g_eventLooper.clearAll();
    motionResetMappings();
    memset(&s_loops, 0, sizeof(s_loops));
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        s_loops.tracks[track].state = LOOP_STREAM_EMPTY;
        s_loops.tracks[track].volumeQ15 = 32767;
    }
    for (uint8_t synth = 0; synth < NUM_SYNTHS; ++synth) g_synths[synth].init();
    for (uint8_t lane = 0; lane < NUM_DRUM_LANES; ++lane)
        g_drumLanes[lane].init(ENG_808, static_cast<uint8_t>(lane % DT_COUNT));
    samplerClearAll();
    performanceStateInit();
}

static void putFile(const char* path, const void* data, size_t bytes) {
    SD.put(path, static_cast<const uint8_t*>(data), bytes);
}
static std::vector<uint8_t> copyFile(const char* path) {
    const std::vector<uint8_t>* bytes = SD.bytes(path);
    assert(bytes);
    return *bytes;
}
static void setVersion(std::vector<uint8_t>& bytes, uint16_t version) {
    assert(bytes.size() >= 6);
    bytes[4] = static_cast<uint8_t>(version & 0xFFu);
    bytes[5] = static_cast<uint8_t>((version >> 8) & 0xFFu);
}

struct __attribute__((packed)) LegacySynth {
    uint8_t oscMode, wtIndex;
    float cutoff, reso, envAmt, ampDec, filtDec, volume;
};
struct __attribute__((packed)) LegacyLane {
    uint8_t engine, type, chokeGroup;
    float volume, tune, decay;
    char sampleName[SAMPLE_NAME_LEN];
};
struct __attribute__((packed)) LegacyCellV1 { uint8_t note, octave, flags; };
struct __attribute__((packed)) LegacyCellV2 {
    uint8_t note[MAX_POLY]; uint8_t oct[MAX_POLY]; uint8_t flags;
};
struct __attribute__((packed)) LegacyV1 {
    uint32_t magic; uint16_t version; uint16_t bpm; uint8_t songLoopStart;
    uint8_t song[64]; LegacySynth synths[NUM_SYNTHS]; LegacyLane lanes[NUM_DRUM_LANES];
    LegacyCellV1 cells[8][NUM_SYNTHS][NUM_STEPS]; uint8_t drums[8][NUM_STEPS];
};
struct __attribute__((packed)) LegacyV2 {
    uint32_t magic; uint16_t version; uint16_t bpm; uint8_t songLoopStart;
    uint8_t song[64]; uint8_t voices[NUM_SYNTHS]; LegacySynth synths[NUM_SYNTHS];
    LegacyLane lanes[NUM_DRUM_LANES]; LegacyCellV2 cells[8][NUM_SYNTHS][NUM_STEPS];
    uint8_t drums[8][NUM_STEPS];
};
static_assert(sizeof(LegacyV1) == 1807, "legacy v1 test layout drifted");
static_assert(sizeof(LegacyV2) == 3346, "legacy v2 test layout drifted");

static LegacySynth validLegacySynth() {
    LegacySynth s = {};
    s.oscMode = OSC_SAW; s.wtIndex = 0; s.cutoff = 0.35f; s.reso = 0.30f;
    s.envAmt = 0.45f; s.ampDec = 0.9995f; s.filtDec = 0.998f; s.volume = 0.70f;
    return s;
}
static LegacyLane validLegacyLane(uint8_t lane) {
    LegacyLane d = {};
    d.engine = ENG_808; d.type = static_cast<uint8_t>(lane % DT_COUNT);
    d.chokeGroup = 0; d.volume = 0.85f; d.tune = 0.0f; d.decay = 1.0f;
    return d;
}
static LegacyV1 makeV1() {
    LegacyV1 file = {};
    file.magic = 0x31584247u; file.version = 1; file.bpm = 111; file.songLoopStart = 0;
    memset(file.song, SONG_EMPTY, sizeof(file.song));
    for (uint8_t s = 0; s < NUM_SYNTHS; ++s) file.synths[s] = validLegacySynth();
    for (uint8_t l = 0; l < NUM_DRUM_LANES; ++l) file.lanes[l] = validLegacyLane(l);
    for (uint8_t p = 0; p < 8; ++p) for (uint8_t s = 0; s < NUM_SYNTHS; ++s)
        for (uint8_t step = 0; step < NUM_STEPS; ++step) {
            file.cells[p][s][step].note = NOTE_EMPTY;
            file.cells[p][s][step].octave = 4;
        }
    return file;
}
static LegacyV2 makeV2() {
    LegacyV2 file = {};
    file.magic = 0x31584247u; file.version = 2; file.bpm = 112; file.songLoopStart = 0;
    memset(file.song, SONG_EMPTY, sizeof(file.song));
    for (uint8_t s = 0; s < NUM_SYNTHS; ++s) {
        file.voices[s] = 1; file.synths[s] = validLegacySynth();
    }
    for (uint8_t l = 0; l < NUM_DRUM_LANES; ++l) file.lanes[l] = validLegacyLane(l);
    for (uint8_t p = 0; p < 8; ++p) for (uint8_t s = 0; s < NUM_SYNTHS; ++s)
        for (uint8_t step = 0; step < NUM_STEPS; ++step) {
            for (uint8_t voice = 0; voice < MAX_POLY; ++voice) {
                file.cells[p][s][step].note[voice] = NOTE_EMPTY;
                file.cells[p][s][step].oct[voice] = 4;
            }
        }
    return file;
}

int main() {
    sdIoInit();
    const char* p1 = DIR_PROJECTS "/P1.gbx";

    // Build one valid v9 image through the production saver, then use its
    // prefix-compatible nested layouts for v3-v8 migration tests.
    SD.reset(); initState();
    g_synths[0].setEngine(SYNTH_ENGINE_MGX);
    g_bpm = 137;
    assert(storageSaveProject(0));
    const std::vector<uint8_t> v9 = copyFile(p1);
    assert(v9.size() == 33805u);

    const LegacyV1 v1 = makeV1();
    SD.remove(p1); SD.remove(DIR_PROJECTS "/P1.gbx.bak"); putFile(p1, &v1, sizeof(v1));
    assert(storageLoadProject(0)); assert(g_bpm == 111); assert(g_synths[0].displayEngine() == SYNTH_ENGINE_MG);

    const LegacyV2 v2 = makeV2();
    SD.remove(p1); SD.remove(DIR_PROJECTS "/P1.gbx.bak"); putFile(p1, &v2, sizeof(v2));
    assert(storageLoadProject(0)); assert(g_bpm == 112); assert(g_synths[0].displayEngine() == SYNTH_ENGINE_MG);

    static const size_t sizes[10] = {0, 1807, 3346, 6226, 15412, 31808, 31816, 31823, 32117, 33805};
    for (uint16_t version = 3; version <= 9; ++version) {
        std::vector<uint8_t> image(v9.begin(), v9.begin() + sizes[version]);
        setVersion(image, version);
        SD.remove(p1); SD.remove(DIR_PROJECTS "/P1.gbx.bak"); SD.put(p1, image);
        g_bpm = 200;
        assert(storageLoadProject(0));
        assert(g_bpm == 137);
        if (version < 8) assert(g_synths[0].displayEngine() == SYNTH_ENGINE_MG);
        else assert(g_synths[0].displayEngine() == SYNTH_ENGINE_MGX);
    }

    // Loading uses the block-boundary exclusive audio mutation gate, and a
    // same-engine project load cannot leave a stale expanded-engine voice alive.
    SD.reset(); initState();
    g_synths[0].setEngine(SYNTH_ENGINE_MGX);
    g_bpm = 123;
    assert(storageSaveProject(2));
    g_synths[0].mgxVoices[0].active = true;
    s_gateBegins = s_gateEnds = 0;
    assert(storageLoadProject(2));
    assert(s_gateBegins == 1 && s_gateEnds == 1);
    assert(!g_synths[0].mgxVoices[0].active);

    // If the gate cannot be acquired, live state is not touched.
    g_bpm = 199;
    s_gateAllowed = false;
    assert(!storageLoadProject(2));
    assert(g_bpm == 199);
    s_gateAllowed = true;

    // Backup fallback and save-after-fallback must preserve the known-good
    // backup instead of replacing it with the corrupt primary.
    SD.reset(); initState();
    const char* p2 = DIR_PROJECTS "/P2.gbx";
    const char* p2bak = DIR_PROJECTS "/P2.gbx.bak";
    const char* p2tmp = DIR_PROJECTS "/P2.gbx.tmp";
    g_bpm = 121; assert(storageSaveProject(1));
    const std::vector<uint8_t> first = copyFile(p2);
    g_bpm = 142; assert(storageSaveProject(1));
    assert(copyFile(p2bak) == first);
    std::vector<uint8_t>* corrupt = SD.mutableBytes(p2);
    assert(corrupt && !corrupt->empty()); (*corrupt)[0] ^= 0xFFu;
    assert(storageLoadProject(1)); assert(g_bpm == 121);
    const std::vector<uint8_t> backupBefore = copyFile(p2bak);
    g_bpm = 166; assert(storageSaveProject(1));
    assert(copyFile(p2bak) == backupBefore);

    // A failed final publish rolls both known-good generations back intact.
    const std::vector<uint8_t> primaryBefore = copyFile(p2);
    const std::vector<uint8_t> backupBeforeFailure = copyFile(p2bak);
    g_bpm = 177;
    SD.failNextRename(p2tmp, p2);
    assert(!storageSaveProject(1));
    assert(copyFile(p2) == primaryBefore);
    assert(copyFile(p2bak) == backupBeforeFailure);

    assert(s_gateBegins >= 10 && s_gateBegins == s_gateEnds + 1); // one intentional gate failure has no end
    return 0;
}
''')

# ---------------------------------------------------------------------------
# storage.cpp production fixes.
# ---------------------------------------------------------------------------
replace_once("storage.cpp",
'''#include "synth_project.h"\n#include "performance_project.h"\n#include <SD.h>''',
'''#include "synth_project.h"\n#include "performance_project.h"\n#include "audio_engine.h"\n#include "project_publish.h"\n#include <SD.h>''')

replace_once("storage.cpp",
'''uint8_t g_curProject = 0;\n\n#define GBX_MAGIC''',
'''uint8_t g_curProject = 0;\n\n// A primary is considered known-good only after this boot successfully loads\n// it or publishes it. If load had to fall back to .bak, the primary must not\n// be rotated over that backup on the next save.\nstatic uint8_t s_knownGoodPrimaryMask = 0;\nstatic bool projectPrimaryKnownGood(uint8_t slot) {\n    return slot < NUM_PROJECT_SLOTS && (s_knownGoodPrimaryMask & (1u << slot)) != 0;\n}\nstatic void projectSetPrimaryKnownGood(uint8_t slot, bool good) {\n    if (slot >= NUM_PROJECT_SLOTS) return;\n    const uint8_t bit = static_cast<uint8_t>(1u << slot);\n    if (good) s_knownGoodPrimaryMask |= bit;\n    else s_knownGoodPrimaryMask &= static_cast<uint8_t>(~bit);\n}\n\n#define GBX_MAGIC''')

replace_once("storage.cpp",
'''    char path[64]; slotPath(slot, path, sizeof(path));\n    char tempPath[72], backupPath[72];\n    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);\n    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);''',
'''    char path[64]; slotPath(slot, path, sizeof(path));\n    char tempPath[72], backupPath[72], previousBackupPath[80];\n    snprintf(tempPath, sizeof(tempPath), "%s.tmp", path);\n    snprintf(backupPath, sizeof(backupPath), "%s.bak", path);\n    snprintf(previousBackupPath, sizeof(previousBackupPath), "%s.bak.prev", path);''')

replace_once("storage.cpp",
'''    {\n        SdIoGuard guard;\n        SD.remove(backupPath);\n        if (SD.exists(path) && !SD.rename(path, backupPath)) {\n            SD.remove(tempPath);\n            return false;\n        }\n        if (!SD.rename(tempPath, path)) {\n            if (SD.exists(backupPath)) SD.rename(backupPath, path);\n            return false;\n        }\n    }\n    return true;''',
'''    bool published = false;\n    {\n        SdIoGuard guard;\n        published = projectPublishTempFile(\n            SD, path, tempPath, backupPath, previousBackupPath,\n            projectPrimaryKnownGood(slot));\n        if (!published) SD.remove(tempPath);\n    }\n    if (published) projectSetPrimaryKnownGood(slot, true);\n    return published;''')

replace_once("storage.cpp",
'''static void applySynth(int s, const SaveSynth& in, uint8_t voices) {\n    SynthTrack& t = g_synths[s];\n    t.forEach''',
'''static void applySynth(int s, const SaveSynth& in, uint8_t voices) {\n    SynthTrack& t = g_synths[s];\n    // storageLoadProject holds the audio mutation gate here. Stop all legacy\n    // and expanded voices before replacing a patch so same-engine loads cannot\n    // leave a latched MGX/FM4 voice running with the new project's settings.\n    t.hardStop();\n    t.forEach''')

replace_once("storage.cpp",
'''    std::unique_ptr<SamplerStage> samplerStage;\n    if (!readProjectFile(path, loaded, version) ||\n        !stageProject(loaded, version, samplerStage)) {\n        // Missing, truncated, structurally corrupt, and semantically corrupt\n        // primaries all fall back to the last atomically published project.\n        char backupPath[72];\n        snprintf(backupPath, sizeof(backupPath), "%s.bak", path);\n        if (!readProjectFile(backupPath, loaded, version) ||\n            !stageProject(loaded, version, samplerStage)) return false;\n    }\n\n    bool wasPlaying = g_playing;\n    g_playing = false;   // pause audio triggering while we swap data\n    bool ok = false;''',
'''    std::unique_ptr<SamplerStage> samplerStage;\n    bool loadedFromBackup = false;\n    if (!readProjectFile(path, loaded, version) ||\n        !stageProject(loaded, version, samplerStage)) {\n        // Missing, truncated, structurally corrupt, and semantically corrupt\n        // primaries all fall back to the last atomically published project.\n        projectSetPrimaryKnownGood(slot, false);\n        loadedFromBackup = true;\n        char backupPath[72];\n        snprintf(backupPath, sizeof(backupPath), "%s.bak", path);\n        if (!readProjectFile(backupPath, loaded, version) ||\n            !stageProject(loaded, version, samplerStage)) return false;\n    }\n\n    bool wasPlaying = g_playing;\n    g_playing = false;   // pause sequencer triggering while we swap data\n    // The audio task still renders while g_playing is false. Acquire an\n    // acknowledged block-boundary gate before touching SynthTrack patches,\n    // voices, master-effects delay state, or vocoder filter state.\n    if (!audioEngineBeginExclusiveMutation(500)) {\n        g_playing = wasPlaying;\n        return false;\n    }\n    bool ok = false;''')

replace_once("storage.cpp",
'''    g_playing = wasPlaying && ok;\n    return ok;\n}''',
'''    audioEngineEndExclusiveMutation();\n    if (ok) projectSetPrimaryKnownGood(slot, !loadedFromBackup);\n    g_playing = wasPlaying && ok;\n    return ok;\n}''')

# ---------------------------------------------------------------------------
# Audio block-boundary exclusive mutation handshake.
# ---------------------------------------------------------------------------
replace_once("audio_engine.h",
'''void audioEngineStart();   // creates the render task on core 0\nAudioDspSnapshot audioEngineDspSnapshot();''',
'''void audioEngineStart();   // creates the render task on core 0\n// Project loading performs several multi-word resets that cannot race core-0\n// rendering. Begin waits for an acknowledged audio block boundary; End lets\n// rendering resume. A timeout fails closed instead of mutating live DSP state.\nbool audioEngineBeginExclusiveMutation(uint32_t timeoutMs);\nvoid audioEngineEndExclusiveMutation();\nAudioDspSnapshot audioEngineDspSnapshot();''')

replace_once("audio_engine.cpp",
'''static TaskHandle_t s_task = nullptr;\nstatic portMUX_TYPE s_dspMux = portMUX_INITIALIZER_UNLOCKED;''',
'''static TaskHandle_t s_task = nullptr;\nalignas(4) static uint8_t s_mutationRequested = 0;\nalignas(4) static uint8_t s_mutationActive = 0;\nstatic portMUX_TYPE s_dspMux = portMUX_INITIALIZER_UNLOCKED;''')

replace_once("audio_engine.cpp",
'''    while (true) {\n        int16_t* buf = buffers[cur];\n        const uint32_t renderStartedUs = micros();''',
'''    while (true) {\n        // Exclusive project mutation handshake. The storage/main task sets the\n        // request and waits for `active`, so it can never suspend us halfway\n        // through a render. We acknowledge only here, between complete blocks.\n        if (__atomic_load_n(&s_mutationRequested, __ATOMIC_ACQUIRE)) {\n            __atomic_store_n(&s_mutationActive, static_cast<uint8_t>(1u), __ATOMIC_RELEASE);\n            while (__atomic_load_n(&s_mutationRequested, __ATOMIC_ACQUIRE))\n                vTaskDelay(1);\n            __atomic_store_n(&s_mutationActive, static_cast<uint8_t>(0u), __ATOMIC_RELEASE);\n            continue;\n        }\n\n        int16_t* buf = buffers[cur];\n        const uint32_t renderStartedUs = micros();''')

replace_once("audio_engine.cpp",
'''void audioEngineStart() {\n    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 1, &s_task, 0);\n}\n\nAudioDspSnapshot audioEngineDspSnapshot()''',
'''void audioEngineStart() {\n    __atomic_store_n(&s_mutationRequested, static_cast<uint8_t>(0u), __ATOMIC_RELEASE);\n    __atomic_store_n(&s_mutationActive, static_cast<uint8_t>(0u), __ATOMIC_RELEASE);\n    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 1, &s_task, 0);\n}\n\nbool audioEngineBeginExclusiveMutation(uint32_t timeoutMs) {\n    if (!s_task) return true;\n    __atomic_store_n(&s_mutationRequested, static_cast<uint8_t>(1u), __ATOMIC_RELEASE);\n    for (uint32_t waited = 0; waited < timeoutMs; ++waited) {\n        if (__atomic_load_n(&s_mutationActive, __ATOMIC_ACQUIRE)) return true;\n        vTaskDelay(1);\n    }\n    __atomic_store_n(&s_mutationRequested, static_cast<uint8_t>(0u), __ATOMIC_RELEASE);\n    return __atomic_load_n(&s_mutationActive, __ATOMIC_ACQUIRE) != 0;\n}\n\nvoid audioEngineEndExclusiveMutation() {\n    if (!s_task) return;\n    __atomic_store_n(&s_mutationRequested, static_cast<uint8_t>(0u), __ATOMIC_RELEASE);\n}\n\nAudioDspSnapshot audioEngineDspSnapshot()''')

# ---------------------------------------------------------------------------
# Long-CTRL must use the same performance-note cleanup as a normal page exit.
# ---------------------------------------------------------------------------
replace_once("input.cpp",
'''#include "performance_scheduler_core.h"\n#include "input.h"''',
'''#include "performance_scheduler_core.h"\n#include "input_page_core.h"\n#include "input.h"''')
replace_once("input.cpp",
'''            if (g_curPage == PAGE_CHORD || g_curPage == PAGE_MEDO)\n                inputStopHiChordPerformanceNotes();''',
'''            if (inputPageNeedsPerformanceStop(g_curPage))\n                inputStopHiChordPerformanceNotes();''')
replace_once("input.cpp",
'''        case ACT_PAGE:\n            g_curPage = PAGE_PATTERN; g_needRedraw = true; break;''',
'''        case ACT_PAGE:\n            if (inputPageNeedsPerformanceStop(g_curPage))\n                inputStopHiChordPerformanceNotes();\n            g_curPage = PAGE_PATTERN; g_needRedraw = true; break;''')

# Exhaustive means all 256 byte values, not a 17-step sample.
replace_once("tests/test_performance_project.cpp",
'''            for (unsigned value = 0; value < 256u; value += 17u) {''',
'''            for (unsigned value = 0; value < 256u; ++value) {''')

# Host suite: execute the production storage path and new pure transaction/page helpers.
replace_once("tests/run_host_tests.sh",
'''"$build_dir/test_performance_project"\n\ng++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \\\n  "$test_dir/test_master_effects.cpp"''',
'''"$build_dir/test_performance_project"\n\ng++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \\\n  -I"$test_dir/stubs" -I"$test_dir/.." \\\n  "$test_dir/test_project_publish.cpp" -o "$build_dir/test_project_publish"\n\n"$build_dir/test_project_publish"\n\ng++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \\\n  "$test_dir/test_input_page_core.cpp" -o "$build_dir/test_input_page_core"\n\n"$build_dir/test_input_page_core"\n\ng++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \\\n  -I"$test_dir/stubs" -I"$test_dir/.." \\\n  "$test_dir/test_storage_project.cpp" "$test_dir/../storage.cpp" \\\n  "$test_dir/../sampler_slots.cpp" "$test_dir/../sd_io_arbiter.cpp" \\\n  "$test_dir/../performance_project.cpp" "$test_dir/../performance_state.cpp" \\\n  "$test_dir/../chord_engine.cpp" "$test_dir/../hichord_performance.cpp" \\\n  "$test_dir/../medo_performance.cpp" "$test_dir/../po_effects.cpp" \\\n  "$test_dir/../master_effects.cpp" "$test_dir/../vocoder.cpp" \\\n  "$test_dir/../synth_project.cpp" "${synth_sources[@]}" \\\n  -o "$build_dir/test_storage_project"\n\n"$build_dir/test_storage_project"\n\ng++ -pipe -std=gnu++11 -O2 -Wall -Wextra -Werror \\\n  "$test_dir/test_master_effects.cpp"''')

# storage.cpp is no longer syntax-only: keep the syntax umbrella, but label it honestly.
replace_once("tests/run_host_tests.sh",
'''echo "storage: GBX v1/v2/v3/v4/v5/v6/v7/v8/v9 layout and syntax checks passed"''',
'''echo "storage: GBX v1-v9 production save/load executed with in-memory SD; syntax umbrella passed"''')

replace_once("tests/run_sanitizers.sh",
'''build_run performance_project "$test_dir/test_performance_project.cpp" \\\n    "$test_dir/../performance_project.cpp" "$test_dir/../performance_state.cpp" \\\n    "$test_dir/../chord_engine.cpp" "$test_dir/../hichord_performance.cpp" \\\n    "$test_dir/../medo_performance.cpp" "$test_dir/../po_effects.cpp" \\\n    "$test_dir/../master_effects.cpp" "$test_dir/../vocoder.cpp" \\\n    "$test_dir/../synth_project.cpp" "${synth_sources[@]}"\nbuild_run audio_cap''',
'''build_run performance_project "$test_dir/test_performance_project.cpp" \\\n    "$test_dir/../performance_project.cpp" "$test_dir/../performance_state.cpp" \\\n    "$test_dir/../chord_engine.cpp" "$test_dir/../hichord_performance.cpp" \\\n    "$test_dir/../medo_performance.cpp" "$test_dir/../po_effects.cpp" \\\n    "$test_dir/../master_effects.cpp" "$test_dir/../vocoder.cpp" \\\n    "$test_dir/../synth_project.cpp" "${synth_sources[@]}"\nbuild_run project_publish -I"$test_dir/stubs" -I"$test_dir/.." \\\n    "$test_dir/test_project_publish.cpp"\nbuild_run input_page "$test_dir/test_input_page_core.cpp"\nbuild_run storage_project -I"$test_dir/stubs" -I"$test_dir/.." \\\n    "$test_dir/test_storage_project.cpp" "$test_dir/../storage.cpp" \\\n    "$test_dir/../sampler_slots.cpp" "$test_dir/../sd_io_arbiter.cpp" \\\n    "$test_dir/../performance_project.cpp" "$test_dir/../performance_state.cpp" \\\n    "$test_dir/../chord_engine.cpp" "$test_dir/../hichord_performance.cpp" \\\n    "$test_dir/../medo_performance.cpp" "$test_dir/../po_effects.cpp" \\\n    "$test_dir/../master_effects.cpp" "$test_dir/../vocoder.cpp" \\\n    "$test_dir/../synth_project.cpp" "${synth_sources[@]}"\nbuild_run audio_cap''')

replace_once("tests/run_sanitizers.sh",
'''echo "Sanitizers (${SANITIZER_SET:-undefined}): core, protocol, three-in-one performance, synth, streaming, event, motion, MIDI and Audio Cap tests passed"''',
'''echo "Sanitizers (${SANITIZER_SET:-undefined}): core, protocol, persistence, three-in-one performance, synth, streaming, event, motion, MIDI and Audio Cap tests passed"''')

# Documentation: remove the stale alpha.2 warning and record the newly executed storage boundary.
replace_once("docs/BUILD_STATUS.md",
'''**Recommendation while alpha.2 remains the newest published release: flash\n`v3.0.0-alpha.1`, which is the last independently audited release, or build\nfrom `main`.** The next tag supersedes both.''',
'''**`v3.0.0-alpha.3` supersedes the known-bad alpha.2 release.** Its source is\nthe independently reviewed/fixed tree described above; later `main` commits\nmay contain documentation or additional audited hardening.''')

replace_once("docs/BUILD_STATUS.md",
'''  Known coverage limits, stated plainly: only `SCALE_MAJOR` is built\n  end-to-end (the other nine scales are covered by name/table checks);\n  `storage.cpp` is syntax-checked but never executed, so GBX *file* I/O,\n  v1–v8 migration and the `.bak` fallback have no host coverage — an\n  in-memory SD stub is the outstanding work there; and `input.cpp`/`ui.cpp`\n  are syntax-checked only, so page interactions are verified by reading, not\n  by running.''',
'''  Storage is now executed through the production `storage.cpp` path against a\n  stateful in-memory SD implementation: v1-v9 project loads, v9 save/load,\n  legacy-engine migration, `.bak` fallback, save-after-fallback preservation,\n  failed-publish rollback, and the audio block-boundary mutation gate all have\n  host regressions. Known coverage limits, stated plainly: only `SCALE_MAJOR`\n  is built end-to-end (the other nine scales are covered by name/table checks);\n  and most `input.cpp`/`ui.cpp` interactions remain syntax-checked/read rather\n  than executed, although the CHORD/MEDO page-exit rule now has a pure host\n  contract test. Physical FatFS timing and UI feel remain hardware gates.''')

replace_once("CLAUDE.md", '`storage.cpp` (GBX v1–v8)', '`storage.cpp` (GBX v1–v9)')

print("storage audit hardening patch applied")
