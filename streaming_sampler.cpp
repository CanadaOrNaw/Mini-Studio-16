#include "streaming_sampler.h"

#include "config.h"
#include "loop_engine.h"
#include "master_recorder.h"
#include "mic_sampler.h"
#include "pcm_ring.h"
#include "sampler.h"
#include "sd_diagnostics.h"
#include "sd_io_arbiter.h"
#include "stem_recorder.h"
#include "wav_file.h"
#include "pitch_detector.h"

#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <memory>
#include <new>
#include <string.h>

namespace {
constexpr uint32_t kRingFrames = 2048;
constexpr uint32_t kPrimeFrames = 1536;
constexpr size_t kReadFrames = 256;
constexpr uint32_t kRecordRingFrames = 4096;
constexpr size_t kWriteFrames = 1024;
constexpr uint8_t kCommandCapacity = 16;
constexpr int32_t kTrimThreshold = 600;
constexpr uint32_t kTrimPrerollMs = 30;

using FirmwareSampleCore = SampleStreamCore<STREAMING_SAMPLE_VOICES, kRingFrames>;

enum CommandType : uint8_t {
    COMMAND_TRIGGER = 1,
    COMMAND_ASSIGN,
    COMMAND_CLEAR,
    COMMAND_STOP_ALL,
    COMMAND_RECORD_START,
};

struct SamplerCommand {
    uint8_t type;
    uint8_t slot;
    uint8_t key;
    uint8_t mode;
    SamplerSlot slotData;
    SamplerLockEntry lock;
    bool hasLock;
    char filename[SAMPLE_NAME_LEN];
    uint32_t sourceRate;
    uint32_t targetFrames;
};

struct WorkerVoice {
    File file;
    uint32_t remainingFrames;
    uint32_t generation;
    bool active;
};

FirmwareSampleCore s_core;
SpscRing<SamplerCommand, kCommandCapacity> s_commands;
SpscRing<int16_t, kRecordRingFrames> s_recordRing;
WorkerVoice s_workers[STREAMING_SAMPLE_VOICES];
File s_recordFile;
char s_recordTempPath[80] = "";
char s_recordFinalPath[80] = "";
char s_recordFilename[SAMPLE_NAME_LEN] = "";
uint32_t s_recordFramesWritten = 0;
uint32_t s_recordSourceRate = 0;
uint8_t s_recordMode = SAMPLER_SLOT_MELODIC;
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;
alignas(4) uint32_t s_pendingCommands = 0;
alignas(4) uint32_t s_pendingMutations = 0;
alignas(4) uint32_t s_commandDrops = 0;
alignas(4) uint32_t s_starts = 0;
alignas(4) uint32_t s_errors = 0;
alignas(4) uint32_t s_maxReadUs = 0;
alignas(4) uint32_t s_recordState = STREAM_SAMPLE_REC_IDLE;
alignas(4) uint32_t s_recordInput = STREAM_SAMPLE_INPUT_NONE;
alignas(4) uint32_t s_recordSlot = 0;
alignas(4) uint32_t s_recordFrames = 0;
alignas(4) uint32_t s_recordTargetFrames = 0;
alignas(4) uint32_t s_recordDropped = 0;
alignas(4) uint32_t s_recordAutoTrim = 0;
alignas(4) uint32_t s_recordHasLoudFrame = 0;
alignas(4) uint32_t s_recordFirstLoudFrame = 0;
alignas(4) uint32_t s_recordLastLoudFrame = 0;

StreamingSamplerRecordState recordState() {
    return static_cast<StreamingSamplerRecordState>(
        __atomic_load_n(&s_recordState, __ATOMIC_ACQUIRE));
}

void setRecordState(StreamingSamplerRecordState state) {
    __atomic_store_n(&s_recordState, static_cast<uint32_t>(state), __ATOMIC_RELEASE);
}

void updateMaximum(uint32_t* value, uint32_t candidate) {
    uint32_t current = __atomic_load_n(value, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(value, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}

void noteError() { __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED); }

void samplePath(const char* filename, char* output, size_t capacity) {
    snprintf(output, capacity, "%s/%s", DIR_SAMPLES, filename);
}

void recordTempPath(uint8_t slot, char* output, size_t capacity) {
    snprintf(output, capacity, "%s/.S%02u.tmp", DIR_SAMPLES,
             static_cast<unsigned>(slot + 1u));
}

bool selectRecordPath(uint8_t slot, char* path, size_t pathCapacity,
                      char* filename, size_t filenameCapacity) {
    for (uint16_t number = 1; number <= 999; ++number) {
        snprintf(filename, filenameCapacity, "S%02u_%03u.wav",
                 static_cast<unsigned>(slot + 1u), static_cast<unsigned>(number));
        samplePath(filename, path, pathCapacity);
        { SdIoGuard guard; if (!SD.exists(path)) return true; }
    }
    return false;
}

void resetRecordWorker() {
    if (s_recordFile) { SdIoGuard guard; s_recordFile.close(); }
    s_recordTempPath[0] = 0;
    s_recordFinalPath[0] = 0;
    s_recordFilename[0] = 0;
    s_recordFramesWritten = 0;
    s_recordSourceRate = 0;
    s_recordMode = SAMPLER_SLOT_MELODIC;
}

void failRecording(bool quarantine) {
    if (s_recordFile) { SdIoGuard guard; s_recordFile.close(); }
    bool quarantined = false;
    if (quarantine && s_recordTempPath[0]) {
        SdIoGuard guard;
        if (SD.exists(s_recordTempPath)) {
            char badPath[96];
            snprintf(badPath, sizeof(badPath), "%s/S%02u_DROPPED.bad", DIR_SAMPLES,
                     static_cast<unsigned>(
                         __atomic_load_n(&s_recordSlot, __ATOMIC_RELAXED) + 1u));
            SD.remove(badPath);
            quarantined = SD.rename(s_recordTempPath, badPath);
        }
    }
    if (!quarantined && s_recordTempPath[0]) {
        SdIoGuard guard;
        SD.remove(s_recordTempPath);
    }
    noteError();
    setRecordState(STREAM_SAMPLE_REC_ERROR);
    __atomic_store_n(&s_recordInput, STREAM_SAMPLE_INPUT_NONE, __ATOMIC_RELEASE);
    resetRecordWorker();
}

bool beginRecordWorker(const SamplerCommand& command) {
    const StreamingSamplerRecordState state = recordState();
    if (state != STREAM_SAMPLE_REC_STARTING && state != STREAM_SAMPLE_REC_STOPPING)
        return false;
    resetRecordWorker();
    recordTempPath(command.slot, s_recordTempPath, sizeof(s_recordTempPath));
    {
        SdIoGuard guard;
        if (SD.exists(s_recordTempPath)) return false;
    }
    if (!selectRecordPath(command.slot, s_recordFinalPath, sizeof(s_recordFinalPath),
                          s_recordFilename, sizeof(s_recordFilename))) return false;
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, command.sourceRate, 0);
    {
        SdIoGuard guard;
        SD.remove(s_recordTempPath);
        s_recordFile = SD.open(s_recordTempPath, FILE_WRITE);
        if (!s_recordFile ||
            s_recordFile.write(header, sizeof(header)) != sizeof(header)) {
            if (s_recordFile) s_recordFile.close();
            SD.remove(s_recordTempPath);
        }
    }
    if (!s_recordFile) {
        resetRecordWorker();
        return false;
    }
    s_recordFramesWritten = 0;
    s_recordSourceRate = command.sourceRate;
    s_recordMode = command.mode;
    if (state == STREAM_SAMPLE_REC_STARTING)
        setRecordState(STREAM_SAMPLE_REC_RECORDING);
    return true;
}

bool pumpRecording() {
    const StreamingSamplerRecordState state = recordState();
    if (!s_recordFile || (state != STREAM_SAMPLE_REC_RECORDING &&
                          state != STREAM_SAMPLE_REC_STOPPING))
        return true;

    int16_t frames[kWriteFrames];
    const size_t count = s_recordRing.pop(frames, kWriteFrames);
    if (count) {
        const uint32_t started = micros();
        const size_t bytes = count * sizeof(int16_t);
        bool ok = false;
        {
            SdIoGuard guard;
            ok = s_recordFile.write(reinterpret_cast<uint8_t*>(frames), bytes) == bytes;
        }
        updateMaximum(&s_maxReadUs, micros() - started);
        if (!ok) {
            failRecording(true);
            return false;
        }
        s_recordFramesWritten += static_cast<uint32_t>(count);
    }

    if (recordState() != STREAM_SAMPLE_REC_STOPPING || s_recordRing.size() != 0)
        return true;
    if (__atomic_load_n(&s_recordDropped, __ATOMIC_RELAXED) != 0 ||
        s_recordFramesWritten < SAMPLER_SLICE_COUNT) {
        failRecording(true);
        return false;
    }

    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, s_recordSourceRate, s_recordFramesWritten);
    bool ok = false;
    {
        SdIoGuard guard;
        ok = s_recordFile.seek(0) &&
             s_recordFile.write(header, sizeof(header)) == sizeof(header);
        if (ok) s_recordFile.flush();
        s_recordFile.close();
        if (ok) ok = SD.rename(s_recordTempPath, s_recordFinalPath);
    }
    const uint8_t slot = static_cast<uint8_t>(
        __atomic_load_n(&s_recordSlot, __ATOMIC_RELAXED));
    const bool autoTrim = __atomic_load_n(&s_recordAutoTrim, __ATOMIC_ACQUIRE) != 0;
    const bool hasLoudFrame =
        __atomic_load_n(&s_recordHasLoudFrame, __ATOMIC_ACQUIRE) != 0;
    if (ok && autoTrim && !hasLoudFrame) ok = false;
    if (ok) {
        ok = g_samplerSlotBank.assign(slot, s_recordFilename, s_recordFramesWritten,
                                      s_recordSourceRate,
                                      static_cast<SamplerSlotMode>(s_recordMode));
    }
    if (ok && autoTrim) {
        const uint32_t first =
            __atomic_load_n(&s_recordFirstLoudFrame, __ATOMIC_ACQUIRE);
        const uint32_t last =
            __atomic_load_n(&s_recordLastLoudFrame, __ATOMIC_ACQUIRE);
        const uint32_t preroll = s_recordSourceRate * kTrimPrerollMs / 1000u;
        const uint32_t start = first > preroll ? first - preroll : 0;
        const uint32_t length = last >= start ? last - start + 1u : 0;
        ok = length >= SAMPLER_SLICE_COUNT &&
             g_samplerSlotBank.setTrim(slot, start, length);
    }
    const StreamingSamplerInput capturedInput = static_cast<StreamingSamplerInput>(
        __atomic_load_n(&s_recordInput, __ATOMIC_ACQUIRE));
    if (ok && capturedInput == STREAM_SAMPLE_INPUT_MIC) {
        std::unique_ptr<int16_t[]> pitchFrames(new (std::nothrow) int16_t[2048]);
        File pitchFile;
        size_t got = 0;
        const uint32_t pitchStart = g_samplerSlotBank.slot(slot).trimStart;
        {
            SdIoGuard guard;
            pitchFile = SD.open(s_recordFinalPath, FILE_READ);
            if (pitchFrames && pitchFile &&
                pitchFile.seek(WAV_PCM_HEADER_BYTES + pitchStart * sizeof(int16_t))) {
                const int bytes = pitchFile.read(reinterpret_cast<uint8_t*>(pitchFrames.get()),
                                                 2048 * sizeof(int16_t));
                if (bytes > 0) got = static_cast<size_t>(bytes);
            }
            if (pitchFile) pitchFile.close();
        }
        const PitchEstimate pitch = PitchDetector::detect(
            pitchFrames.get(), got / sizeof(int16_t), s_recordSourceRate);
        if (pitch.midiNote >= 0 && pitch.midiNote <= 127) {
            g_samplerSlotBank.beginEdit(slot);
            g_samplerSlotBank.slot(slot).rootMidi = static_cast<uint8_t>(pitch.midiNote);
            g_samplerSlotBank.endEdit(slot);
        }
    }
    if (!ok) {
        { SdIoGuard guard; if (SD.exists(s_recordFinalPath)) SD.remove(s_recordFinalPath); }
        failRecording(false);
        return false;
    }
    setRecordState(STREAM_SAMPLE_REC_COMPLETE);
    __atomic_store_n(&s_recordInput, STREAM_SAMPLE_INPUT_NONE, __ATOMIC_RELEASE);
    Serial.printf("SAMPLE_RECORD state=complete slot=%u file=%s frames=%lu rate=%lu\n",
                  static_cast<unsigned>(slot + 1u), s_recordFilename,
                  static_cast<unsigned long>(s_recordFramesWritten),
                  static_cast<unsigned long>(s_recordSourceRate));
    resetRecordWorker();
    return true;
}

void recoverInterruptedRecordings() {
    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        char tempPath[80];
        recordTempPath(slot, tempPath, sizeof(tempPath));
        File file;
        { SdIoGuard guard; if (!SD.exists(tempPath)) continue; file = SD.open(tempPath, "r+"); }
        bool ok = static_cast<bool>(file);
        uint32_t sourceRate = SAMPLE_RATE;
        uint32_t frames = 0;
        if (ok) {
            uint8_t oldHeader[WAV_PCM_HEADER_BYTES];
            WavMono16Info info = {};
            {
                SdIoGuard guard;
                ok = file.seek(0) &&
                     file.read(oldHeader, sizeof(oldHeader)) == static_cast<int>(sizeof(oldHeader));
            }
            ok = ok && wavParseCanonicalMono16Header(oldHeader, info);
            if (ok) sourceRate = info.sampleRate;
            const WavRecoveryPlan plan = wavPlanMono16Recovery(
                [&file]() { SdIoGuard guard; return static_cast<uint32_t>(file.size()); }());
            ok = ok && plan.recoverable;
            frames = plan.frames;
            if (ok) {
                uint8_t header[WAV_PCM_HEADER_BYTES];
                wavBuildMono16Header(header, sourceRate, frames);
                SdIoGuard guard;
                ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
                if (ok) file.flush();
            }
            { SdIoGuard guard; file.close(); }
        }
        char recoveredPath[96];
        char recoveredName[SAMPLE_NAME_LEN];
        bool havePath = selectRecordPath(slot, recoveredPath, sizeof(recoveredPath),
                                         recoveredName, sizeof(recoveredName));
        if (ok && havePath) { SdIoGuard guard; ok = SD.rename(tempPath, recoveredPath); }
        if (!ok) {
            char badPath[96];
            snprintf(badPath, sizeof(badPath), "%s/S%02u_RECOVER.bad", DIR_SAMPLES,
                     static_cast<unsigned>(slot + 1u));
            SdIoGuard guard;
            SD.remove(badPath);
            if (!SD.rename(tempPath, badPath)) noteError();
        }
        Serial.printf("SAMPLE_RECOVERY state=%s slot=%u frames=%lu rate=%lu\n",
                      ok ? "recovered" : "quarantined",
                      static_cast<unsigned>(slot + 1u),
                      static_cast<unsigned long>(frames),
                      static_cast<unsigned long>(sourceRate));
    }
}

bool readHeader(File& file, WavMono16Info& info) {
    uint8_t header[WAV_PCM_HEADER_BYTES];
    const uint32_t started = micros();
    bool ioOk = false;
    uint32_t fileBytes = 0;
    {
        SdIoGuard guard;
        ioOk = file.seek(0) &&
            file.read(header, sizeof(header)) == static_cast<int>(sizeof(header));
        fileBytes = static_cast<uint32_t>(file.size());
    }
    const bool ok = ioOk && wavParseCanonicalMono16Header(header, info) &&
        info.frames >= SAMPLER_SLICE_COUNT &&
        fileBytes >= WAV_PCM_HEADER_BYTES + info.frames * sizeof(int16_t);
    updateMaximum(&s_maxReadUs, micros() - started);
    return ok;
}

void closeVoice(uint8_t voice) {
    if (voice >= STREAMING_SAMPLE_VOICES) return;
    if (s_workers[voice].file) { SdIoGuard guard; s_workers[voice].file.close(); }
    s_workers[voice].active = false;
    s_workers[voice].remainingFrames = 0;
}

void stopAllVoices() {
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
        s_core.stop(voice);
        closeVoice(voice);
    }
}

uint32_t playbackIncrement(uint32_t sourceRate, int32_t pitchQ8) {
    float semitones = static_cast<float>(pitchQ8) / 256.0f;
    if (semitones < -24.0f) semitones = -24.0f;
    else if (semitones > 24.0f) semitones = 24.0f;
    float increment = static_cast<float>(sourceRate) / static_cast<float>(SAMPLE_RATE) *
                      powf(2.0f, semitones / 12.0f);
    if (increment < 0.125f) increment = 0.125f;
    else if (increment > 8.0f) increment = 8.0f;
    return static_cast<uint32_t>(increment * 65536.0f + 0.5f);
}

SamplerRegion lockedRegion(const SamplerRegion& input, const SamplerLockEntry& lock) {
    if ((lock.flags & SAMPLER_LOCK_TRIM) == 0) return input;
    uint32_t offset = static_cast<uint32_t>(
        (static_cast<uint64_t>(input.lengthFrames - 1u) * lock.trimStartQ15) / 32767u);
    // P3 (reconciliation report): a fully right-shifted trim lock could
    // leave a 1-frame region, which startVoice rejects — silently muting
    // the step. Clamp the offset so at least two playable frames remain.
    if (input.lengthFrames >= 2 && offset > input.lengthFrames - 2u)
        offset = input.lengthFrames - 2u;
    const uint32_t available = input.lengthFrames - offset;
    uint32_t length = static_cast<uint32_t>(
        (static_cast<uint64_t>(available) * lock.trimLengthQ15) / 32767u);
    if (length < 2) length = min<uint32_t>(2, available);
    return SamplerRegion{input.startFrame + offset, length};
}

bool pumpVoice(uint8_t voice) {
    WorkerVoice& worker = s_workers[voice];
    if (!worker.active) return false;
    const SampleStreamVoiceSnapshot snapshot = s_core.snapshot(voice);
    if (snapshot.generation != worker.generation) {
        closeVoice(voice);
        return false;
    }
    if (worker.remainingFrames == 0) {
        s_core.markEof(voice);
        return true;
    }
    const uint32_t free = s_core.freeSpace(voice);
    if (free == 0) return true;
    const size_t wanted = min<size_t>(min<uint32_t>(free, worker.remainingFrames),
                                      kReadFrames);
    int16_t frames[kReadFrames];
    const uint32_t started = micros();
    int bytes = 0;
    { SdIoGuard guard; bytes = worker.file.read(reinterpret_cast<uint8_t*>(frames),
                                                wanted * sizeof(int16_t)); }
    updateMaximum(&s_maxReadUs, micros() - started);
    if (bytes != static_cast<int>(wanted * sizeof(int16_t)) ||
        s_core.push(voice, frames, wanted) != wanted) {
        s_core.markError(voice);
        closeVoice(voice);
        noteError();
        return false;
    }
    worker.remainingFrames -= static_cast<uint32_t>(wanted);
    if (worker.remainingFrames == 0) s_core.markEof(voice);
    return true;
}

bool startVoice(const SamplerCommand& command) {
    SamplerRegion region = {};
    const SamplerSlot& slot = command.slotData;
    if (slot.mode == SAMPLER_SLOT_EMPTY || command.key >= SAMPLER_SLICE_COUNT)
        return false;
    region = slot.mode == SAMPLER_SLOT_SLICED
        ? slot.slices[command.key] : SamplerRegion{slot.trimStart, slot.trimLength};
    if (command.hasLock) region = lockedRegion(region, command.lock);
    if (region.lengthFrames < 2 ||
        static_cast<uint64_t>(region.startFrame) + region.lengthFrames > slot.sourceFrames)
        return false;

    const uint8_t voice = s_core.allocateVoice();
    closeVoice(voice);
    char path[96];
    samplePath(slot.filename, path, sizeof(path));
    File file;
    { SdIoGuard guard; file = SD.open(path, FILE_READ); }
    WavMono16Info info = {};
    if (!file || !readHeader(file, info) || info.frames != slot.sourceFrames ||
        info.sampleRate != slot.sourceRate ||
        [&file, &region]() { SdIoGuard guard; return file.seek(
            WAV_PCM_HEADER_BYTES + region.startFrame * sizeof(int16_t)); }() == false) {
        if (file) { SdIoGuard guard; file.close(); }
        return false;
    }

    int32_t pitch = slot.pitchQ8;
    if (slot.mode == SAMPLER_SLOT_MELODIC)
        pitch += static_cast<int32_t>(60 + command.key - slot.rootMidi) * 256;
    uint16_t gain = slot.gainQ15;
    uint16_t cutoff = slot.cutoffQ15;
    uint16_t resonance = slot.resonanceQ15;
    if (command.hasLock) {
        if (command.lock.flags & SAMPLER_LOCK_PITCH) pitch += command.lock.pitchQ8;
        if (command.lock.flags & SAMPLER_LOCK_GAIN) gain = command.lock.gainQ15;
        if (command.lock.flags & SAMPLER_LOCK_FILTER) {
            cutoff = command.lock.cutoffQ15;
            resonance = command.lock.resonanceQ15;
        }
    }
    if (!s_core.prepare(voice, command.slot, region.lengthFrames,
                        playbackIncrement(slot.sourceRate, pitch), gain,
                        cutoff, resonance)) {
        { SdIoGuard guard; file.close(); }
        return false;
    }
    WorkerVoice& worker = s_workers[voice];
    worker.file = file;
    worker.remainingFrames = region.lengthFrames;
    worker.generation = s_core.snapshot(voice).generation;
    worker.active = true;
    while (s_core.snapshot(voice).bufferedFrames < kPrimeFrames &&
           worker.remainingFrames > 0)
        if (!pumpVoice(voice)) return false;
    if (!s_core.arm(voice, min<uint32_t>(kPrimeFrames, region.lengthFrames))) {
        closeVoice(voice);
        s_core.markError(voice);
        return false;
    }
    __atomic_add_fetch(&s_starts, 1u, __ATOMIC_RELAXED);
    return true;
}

bool assignSlot(const SamplerCommand& command) {
    char path[96];
    samplePath(command.filename, path, sizeof(path));
    File file;
    { SdIoGuard guard; file = SD.open(path, FILE_READ); }
    WavMono16Info info = {};
    const bool ok = file && readHeader(file, info) &&
        g_samplerSlotBank.assign(command.slot, command.filename, info.frames,
                                 info.sampleRate,
                                 static_cast<SamplerSlotMode>(command.mode));
    if (file) { SdIoGuard guard; file.close(); }
    return ok;
}

void processCommand(const SamplerCommand& command) {
    bool ok = true;
    switch (command.type) {
        case COMMAND_TRIGGER: ok = startVoice(command); break;
        case COMMAND_ASSIGN:
            ok = assignSlot(command);
            // P3 (reconciliation report): a mic/resample fallback reserves
            // the slot bit before this async assign lands; once the bank
            // entry is authoritative the reservation must not outlive it,
            // or a later slot clear leaves the bit set and the slot is
            // skipped by every future reservation until project reload.
            if (ok) samplerReleaseStreamReference(
                samplerMakeStreamReference(command.slot));
            break;
        case COMMAND_CLEAR:
            ok = g_samplerSlotBank.remove(command.slot);
            if (ok) samplerReleaseStreamReference(
                samplerMakeStreamReference(command.slot));  // P3, see above
            break;
        case COMMAND_STOP_ALL: stopAllVoices(); break;
        case COMMAND_RECORD_START: ok = beginRecordWorker(command); break;
        default: ok = false; break;
    }
    if (!ok) {
        if (command.type == COMMAND_RECORD_START) failRecording(false);
        else noteError();
    }
    if (command.type == COMMAND_ASSIGN || command.type == COMMAND_CLEAR)
        __atomic_sub_fetch(&s_pendingMutations, 1u, __ATOMIC_ACQ_REL);
    __atomic_sub_fetch(&s_pendingCommands, 1u, __ATOMIC_ACQ_REL);
}

void storageTask(void*) {
    while (true) {
        SamplerCommand command = {};
        while (s_commands.popOne(command)) processCommand(command);
        pumpRecording();
        for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
            const SampleStreamState state = s_core.voiceState(voice);
            if (state == SAMPLE_STREAM_COMPLETE || state == SAMPLE_STREAM_UNDERRUN ||
                state == SAMPLE_STREAM_ERROR) {
                closeVoice(voice);
            } else if ((state == SAMPLE_STREAM_PREPARING || state == SAMPLE_STREAM_PLAYING) &&
                       s_workers[voice].active && s_core.freeSpace(voice) >= kReadFrames) {
                pumpVoice(voice);
            }
        }
        vTaskDelay(1);
    }
}

bool queueCommand(const SamplerCommand& command) {
    if (!s_sdMounted || !s_task) {
        __atomic_add_fetch(&s_commandDrops, 1u, __ATOMIC_RELAXED);
        return false;
    }
    const bool mutation = command.type == COMMAND_ASSIGN ||
                          command.type == COMMAND_CLEAR;
    // Publish the counters before the command becomes visible to the worker.
    // Otherwise a fast worker can pop/decrement a just-pushed command before
    // this producer increments the corresponding count.
    __atomic_add_fetch(&s_pendingCommands, 1u, __ATOMIC_ACQ_REL);
    if (mutation) __atomic_add_fetch(&s_pendingMutations, 1u, __ATOMIC_ACQ_REL);
    if (!s_commands.pushOne(command)) {
        if (mutation)
            __atomic_sub_fetch(&s_pendingMutations, 1u, __ATOMIC_ACQ_REL);
        __atomic_sub_fetch(&s_pendingCommands, 1u, __ATOMIC_ACQ_REL);
        __atomic_add_fetch(&s_commandDrops, 1u, __ATOMIC_RELAXED);
        return false;
    }
    return true;
}
}  // namespace

void streamingSamplerInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    s_core.reset();
    s_commands.reset();
    s_recordRing.reset();
    resetRecordWorker();
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
        s_workers[voice].remainingFrames = 0;
        s_workers[voice].generation = 0;
        s_workers[voice].active = false;
    }
    __atomic_store_n(&s_pendingCommands, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_pendingMutations, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_commandDrops, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_starts, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_errors, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_maxReadUs, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordState,
                     sdMounted ? STREAM_SAMPLE_REC_IDLE : STREAM_SAMPLE_REC_ERROR,
                     __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordInput, STREAM_SAMPLE_INPUT_NONE, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordSlot, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordFrames, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordTargetFrames, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordDropped, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordAutoTrim, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordHasLoudFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordFirstLoudFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordLastLoudFrame, 0u, __ATOMIC_RELEASE);
    if (!sdMounted) return;
    recoverInterruptedRecordings();
    if (xTaskCreatePinnedToCore(storageTask, "sample_sd", 6144, nullptr, 1,
                                &s_task, 1) != pdPASS) {
        s_task = nullptr;
        noteError();
    }
}

int32_t streamingSamplerRender() {
    return s_sdMounted ? s_core.render() : 0;
}

bool streamingSamplerTrigger(uint8_t slot, uint8_t key,
                             const SamplerLockEntry* lock) {
    if (slot >= SAMPLER_SLOT_COUNT || key >= SAMPLER_SLICE_COUNT) return false;
    SamplerCommand command = {};
    command.type = COMMAND_TRIGGER;
    command.slot = slot;
    command.key = key;
    // P3 (reconciliation report): the sampler worker mutates the same slot
    // struct this task is copying; a torn copy could drop the trigger or
    // play a wrong region. Take a seqlock-consistent snapshot instead.
    if (!g_samplerSlotBank.snapshotSlot(slot, command.slotData)) return false;
    if (command.slotData.mode == SAMPLER_SLOT_EMPTY) return false;
    if (lock) { command.lock = *lock; command.hasLock = true; }
    return queueCommand(command);
}

bool streamingSamplerAssign(uint8_t slot, const char* filename,
                            SamplerSlotMode mode) {
    if (slot >= SAMPLER_SLOT_COUNT || !filename || !filename[0] ||
        strlen(filename) >= SAMPLE_NAME_LEN || strchr(filename, '/') ||
        strchr(filename, '\\') ||
        (mode != SAMPLER_SLOT_MELODIC && mode != SAMPLER_SLOT_SLICED) ||
        (recordState() == STREAM_SAMPLE_REC_STARTING ||
         recordState() == STREAM_SAMPLE_REC_RECORDING ||
         recordState() == STREAM_SAMPLE_REC_STOPPING))
        return false;
    SamplerCommand command = {};
    command.type = COMMAND_ASSIGN;
    command.slot = slot;
    command.mode = mode;
    strcpy(command.filename, filename);
    return queueCommand(command);
}

bool streamingSamplerClear(uint8_t slot) {
    const StreamingSamplerRecordState state = recordState();
    if (slot >= SAMPLER_SLOT_COUNT || state == STREAM_SAMPLE_REC_STARTING ||
        state == STREAM_SAMPLE_REC_RECORDING || state == STREAM_SAMPLE_REC_STOPPING)
        return false;
    SamplerCommand command = {};
    command.type = COMMAND_CLEAR;
    command.slot = slot;
    return queueCommand(command);
}

bool streamingSamplerBeginRecord(uint8_t slot, SamplerSlotMode mode,
                                 uint32_t sourceRate, StreamingSamplerInput input,
                                 uint32_t maximumSourceFrames, bool autoTrim) {
    const StreamingSamplerRecordState state = recordState();
    if (!s_sdMounted || !s_task || slot >= SAMPLER_SLOT_COUNT || sourceRate == 0 ||
        (mode != SAMPLER_SLOT_MELODIC && mode != SAMPLER_SLOT_SLICED) ||
        (input != STREAM_SAMPLE_INPUT_BUS && input != STREAM_SAMPLE_INPUT_MIC) ||
        state == STREAM_SAMPLE_REC_STARTING || state == STREAM_SAMPLE_REC_RECORDING ||
        state == STREAM_SAMPLE_REC_STOPPING || masterRecorderIsBusy() ||
        stemRecorderIsBusy() || sdDiagnosticsIsRunning() || loopEngineIsRecording() ||
        micSamplerHasPendingCommit() ||
        (input == STREAM_SAMPLE_INPUT_BUS && micRecActive()))
        return false;

    const SamplerSlot& previous = g_samplerSlotBank.slot(slot);
    const uint32_t availableQuota = g_samplerSlotBank.quotaRemainingFrames() +
                                    previous.quotaFrames;
    const uint64_t scaled = static_cast<uint64_t>(availableQuota) * sourceRate;
    uint32_t target = static_cast<uint32_t>(scaled / SAMPLE_RATE);
    if (maximumSourceFrames != 0 && target > maximumSourceFrames)
        target = maximumSourceFrames;
    if (target < SAMPLER_SLICE_COUNT) return false;

    SamplerCommand command = {};
    command.type = COMMAND_RECORD_START;
    command.slot = slot;
    command.mode = mode;
    command.sourceRate = sourceRate;
    command.targetFrames = target;
    s_recordRing.reset();
    __atomic_store_n(&s_recordSlot, slot, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordInput, input, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordFrames, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordTargetFrames, target, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordDropped, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordAutoTrim, autoTrim ? 1u : 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordHasLoudFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordFirstLoudFrame, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordLastLoudFrame, 0u, __ATOMIC_RELEASE);
    setRecordState(STREAM_SAMPLE_REC_STARTING);
    if (queueCommand(command)) return true;
    setRecordState(STREAM_SAMPLE_REC_ERROR);
    __atomic_store_n(&s_recordInput, STREAM_SAMPLE_INPUT_NONE, __ATOMIC_RELEASE);
    return false;
}

size_t streamingSamplerRecordPush(StreamingSamplerInput input,
                                  const int16_t* frames, size_t count) {
    if (!frames || count == 0 ||
        __atomic_load_n(&s_recordInput, __ATOMIC_ACQUIRE) != input)
        return 0;
    const StreamingSamplerRecordState state = recordState();
    if (state != STREAM_SAMPLE_REC_STARTING && state != STREAM_SAMPLE_REC_RECORDING)
        return 0;
    const uint32_t produced = __atomic_load_n(&s_recordFrames, __ATOMIC_RELAXED);
    const uint32_t target = __atomic_load_n(&s_recordTargetFrames, __ATOMIC_RELAXED);
    if (produced >= target) {
        setRecordState(STREAM_SAMPLE_REC_STOPPING);
        return 0;
    }
    const size_t wanted = min<size_t>(count, target - produced);
    const size_t pushed = s_recordRing.push(frames, wanted);
    if (__atomic_load_n(&s_recordAutoTrim, __ATOMIC_RELAXED) != 0) {
        for (size_t index = 0; index < pushed; ++index) {
            const int32_t sample = frames[index];
            const int32_t magnitude = sample < 0 ? -sample : sample;
            if (magnitude < kTrimThreshold) continue;
            const uint32_t absolute = produced + static_cast<uint32_t>(index);
            uint32_t expected = 0;
            if (__atomic_compare_exchange_n(&s_recordHasLoudFrame, &expected, 1u,
                                            false, __ATOMIC_ACQ_REL,
                                            __ATOMIC_ACQUIRE))
                __atomic_store_n(&s_recordFirstLoudFrame, absolute,
                                 __ATOMIC_RELEASE);
            __atomic_store_n(&s_recordLastLoudFrame, absolute, __ATOMIC_RELEASE);
        }
    }
    __atomic_add_fetch(&s_recordFrames, static_cast<uint32_t>(pushed), __ATOMIC_RELAXED);
    if (pushed < wanted) {
        __atomic_add_fetch(&s_recordDropped, static_cast<uint32_t>(wanted - pushed),
                           __ATOMIC_RELAXED);
        setRecordState(STREAM_SAMPLE_REC_STOPPING);
    } else if (produced + pushed >= target) {
        setRecordState(STREAM_SAMPLE_REC_STOPPING);
    }
    return pushed;
}

bool streamingSamplerStopRecord() {
    const StreamingSamplerRecordState state = recordState();
    if (state != STREAM_SAMPLE_REC_STARTING && state != STREAM_SAMPLE_REC_RECORDING)
        return false;
    setRecordState(STREAM_SAMPLE_REC_STOPPING);
    return true;
}

void streamingSamplerStopAll() {
    SamplerCommand command = {};
    command.type = COMMAND_STOP_ALL;
    queueCommand(command);
}

bool streamingSamplerBusy() {
    const StreamingSamplerRecordState record = recordState();
    if (record == STREAM_SAMPLE_REC_STARTING || record == STREAM_SAMPLE_REC_RECORDING ||
        record == STREAM_SAMPLE_REC_STOPPING)
        return true;
    if (__atomic_load_n(&s_pendingCommands, __ATOMIC_ACQUIRE) != 0) return true;
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
        const SampleStreamState state = s_core.voiceState(voice);
        if (state == SAMPLE_STREAM_PREPARING || state == SAMPLE_STREAM_PLAYING) return true;
    }
    return false;
}

bool streamingSamplerIsRecording() {
    const StreamingSamplerRecordState state = recordState();
    return state == STREAM_SAMPLE_REC_STARTING || state == STREAM_SAMPLE_REC_RECORDING ||
           state == STREAM_SAMPLE_REC_STOPPING;
}

bool streamingSamplerHasPendingMutation() {
    return __atomic_load_n(&s_pendingMutations, __ATOMIC_ACQUIRE) != 0;
}

StreamingSamplerSnapshot streamingSamplerSnapshot() {
    StreamingSamplerSnapshot result = {};
    result.available = s_sdMounted && s_task;
    result.queuedCommands = __atomic_load_n(&s_pendingCommands, __ATOMIC_ACQUIRE);
    result.commandDrops = __atomic_load_n(&s_commandDrops, __ATOMIC_RELAXED);
    result.starts = __atomic_load_n(&s_starts, __ATOMIC_RELAXED);
    result.errors = __atomic_load_n(&s_errors, __ATOMIC_RELAXED);
    result.maxReadUs = __atomic_load_n(&s_maxReadUs, __ATOMIC_RELAXED);
    result.recordState = recordState();
    result.recordInput = static_cast<StreamingSamplerInput>(
        __atomic_load_n(&s_recordInput, __ATOMIC_ACQUIRE));
    result.recordSlot = static_cast<uint8_t>(
        __atomic_load_n(&s_recordSlot, __ATOMIC_RELAXED));
    result.recordFrames = __atomic_load_n(&s_recordFrames, __ATOMIC_RELAXED);
    result.recordTargetFrames = __atomic_load_n(&s_recordTargetFrames, __ATOMIC_RELAXED);
    result.recordDroppedFrames = __atomic_load_n(&s_recordDropped, __ATOMIC_RELAXED);
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice)
        result.voices[voice] = s_core.snapshot(voice);
    return result;
}

const char* sampleStreamStateName(SampleStreamState state) {
    switch (state) {
        case SAMPLE_STREAM_IDLE: return "idle";
        case SAMPLE_STREAM_PREPARING: return "preparing";
        case SAMPLE_STREAM_PLAYING: return "playing";
        case SAMPLE_STREAM_COMPLETE: return "complete";
        case SAMPLE_STREAM_UNDERRUN: return "underrun";
        case SAMPLE_STREAM_ERROR: return "error";
        default: return "unknown";
    }
}
