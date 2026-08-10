#include "streaming_sampler.h"

#include "config.h"
#include "pcm_ring.h"
#include "wav_file.h"

#include <Arduino.h>
#include <SD.h>
#include <math.h>
#include <string.h>

namespace {
constexpr uint32_t kRingFrames = 2048;
constexpr uint32_t kPrimeFrames = 1536;
constexpr size_t kReadFrames = 256;
constexpr uint8_t kCommandCapacity = 32;

using FirmwareSampleCore = SampleStreamCore<STREAMING_SAMPLE_VOICES, kRingFrames>;

enum CommandType : uint8_t {
    COMMAND_TRIGGER = 1,
    COMMAND_ASSIGN,
    COMMAND_CLEAR,
    COMMAND_STOP_ALL,
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
};

struct WorkerVoice {
    File file;
    uint32_t remainingFrames;
    uint32_t generation;
    bool active;
};

FirmwareSampleCore s_core;
SpscRing<SamplerCommand, kCommandCapacity> s_commands;
WorkerVoice s_workers[STREAMING_SAMPLE_VOICES];
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;
alignas(4) uint32_t s_pendingCommands = 0;
alignas(4) uint32_t s_commandDrops = 0;
alignas(4) uint32_t s_starts = 0;
alignas(4) uint32_t s_errors = 0;
alignas(4) uint32_t s_maxReadUs = 0;

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

bool readHeader(File& file, WavMono16Info& info) {
    uint8_t header[WAV_PCM_HEADER_BYTES];
    const uint32_t started = micros();
    const bool ok = file.seek(0) &&
        file.read(header, sizeof(header)) == static_cast<int>(sizeof(header)) &&
        wavParseCanonicalMono16Header(header, info) && info.frames >= SAMPLER_SLICE_COUNT &&
        file.size() >= WAV_PCM_HEADER_BYTES + info.frames * sizeof(int16_t);
    updateMaximum(&s_maxReadUs, micros() - started);
    return ok;
}

void closeVoice(uint8_t voice) {
    if (voice >= STREAMING_SAMPLE_VOICES) return;
    if (s_workers[voice].file) s_workers[voice].file.close();
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
    const uint32_t offset = static_cast<uint32_t>(
        (static_cast<uint64_t>(input.lengthFrames - 1u) * lock.trimStartQ15) / 32767u);
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
    const int bytes = worker.file.read(reinterpret_cast<uint8_t*>(frames),
                                       wanted * sizeof(int16_t));
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
    File file = SD.open(path, FILE_READ);
    WavMono16Info info = {};
    if (!file || !readHeader(file, info) || info.frames != slot.sourceFrames ||
        info.sampleRate != slot.sourceRate ||
        !file.seek(WAV_PCM_HEADER_BYTES + region.startFrame * sizeof(int16_t))) {
        if (file) file.close();
        return false;
    }

    int32_t pitch = slot.pitchQ8;
    if (slot.mode == SAMPLER_SLOT_MELODIC)
        pitch += static_cast<int32_t>(command.key) * 256;
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
        file.close();
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
    File file = SD.open(path, FILE_READ);
    WavMono16Info info = {};
    const bool ok = file && readHeader(file, info) &&
        g_samplerSlotBank.assign(command.slot, command.filename, info.frames,
                                 info.sampleRate,
                                 static_cast<SamplerSlotMode>(command.mode));
    if (file) file.close();
    return ok;
}

void processCommand(const SamplerCommand& command) {
    bool ok = true;
    switch (command.type) {
        case COMMAND_TRIGGER: ok = startVoice(command); break;
        case COMMAND_ASSIGN: ok = assignSlot(command); break;
        case COMMAND_CLEAR: ok = g_samplerSlotBank.remove(command.slot); break;
        case COMMAND_STOP_ALL: stopAllVoices(); break;
        default: ok = false; break;
    }
    if (!ok) noteError();
    __atomic_sub_fetch(&s_pendingCommands, 1u, __ATOMIC_ACQ_REL);
}

void storageTask(void*) {
    while (true) {
        SamplerCommand command = {};
        while (s_commands.popOne(command)) processCommand(command);
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
    if (!s_sdMounted || !s_task || !s_commands.pushOne(command)) {
        __atomic_add_fetch(&s_commandDrops, 1u, __ATOMIC_RELAXED);
        return false;
    }
    __atomic_add_fetch(&s_pendingCommands, 1u, __ATOMIC_ACQ_REL);
    return true;
}
}  // namespace

void streamingSamplerInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    s_core.reset();
    s_commands.reset();
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
        s_workers[voice].remainingFrames = 0;
        s_workers[voice].generation = 0;
        s_workers[voice].active = false;
    }
    __atomic_store_n(&s_pendingCommands, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_commandDrops, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_starts, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_errors, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_maxReadUs, 0u, __ATOMIC_RELEASE);
    if (!sdMounted) return;
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
    const SamplerSlot& source = g_samplerSlotBank.slot(slot);
    if (source.mode == SAMPLER_SLOT_EMPTY) return false;
    SamplerCommand command = {};
    command.type = COMMAND_TRIGGER;
    command.slot = slot;
    command.key = key;
    command.slotData = source;
    if (lock) { command.lock = *lock; command.hasLock = true; }
    return queueCommand(command);
}

bool streamingSamplerAssign(uint8_t slot, const char* filename,
                            SamplerSlotMode mode) {
    if (slot >= SAMPLER_SLOT_COUNT || !filename || !filename[0] ||
        strlen(filename) >= SAMPLE_NAME_LEN || strchr(filename, '/') ||
        strchr(filename, '\\') ||
        (mode != SAMPLER_SLOT_MELODIC && mode != SAMPLER_SLOT_SLICED))
        return false;
    SamplerCommand command = {};
    command.type = COMMAND_ASSIGN;
    command.slot = slot;
    command.mode = mode;
    strcpy(command.filename, filename);
    return queueCommand(command);
}

bool streamingSamplerClear(uint8_t slot) {
    if (slot >= SAMPLER_SLOT_COUNT) return false;
    SamplerCommand command = {};
    command.type = COMMAND_CLEAR;
    command.slot = slot;
    return queueCommand(command);
}

void streamingSamplerStopAll() {
    SamplerCommand command = {};
    command.type = COMMAND_STOP_ALL;
    queueCommand(command);
}

bool streamingSamplerBusy() {
    if (__atomic_load_n(&s_pendingCommands, __ATOMIC_ACQUIRE) != 0) return true;
    for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
        const SampleStreamState state = s_core.voiceState(voice);
        if (state == SAMPLE_STREAM_PREPARING || state == SAMPLE_STREAM_PLAYING) return true;
    }
    return false;
}

StreamingSamplerSnapshot streamingSamplerSnapshot() {
    StreamingSamplerSnapshot result = {};
    result.available = s_sdMounted && s_task;
    result.queuedCommands = __atomic_load_n(&s_pendingCommands, __ATOMIC_ACQUIRE);
    result.commandDrops = __atomic_load_n(&s_commandDrops, __ATOMIC_RELAXED);
    result.starts = __atomic_load_n(&s_starts, __ATOMIC_RELAXED);
    result.errors = __atomic_load_n(&s_errors, __ATOMIC_RELAXED);
    result.maxReadUs = __atomic_load_n(&s_maxReadUs, __ATOMIC_RELAXED);
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
