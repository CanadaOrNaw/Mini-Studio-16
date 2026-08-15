#include "../storage.h"
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

    assert(s_gateBegins >= 3 && s_gateBegins == s_gateEnds + 1); // one intentional gate failure has no end
    return 0;
}
