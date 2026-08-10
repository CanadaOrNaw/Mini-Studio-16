// ============================================================
// CardputerGroovebox - sampler.cpp
// ============================================================
#include "sampler.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include "sd_io_arbiter.h"
#include "sample_reference.h"
#include "streaming_sampler.h"
#include "wav_file.h"
#include <SD.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

int16_t*   g_samplePool   = nullptr;
uint32_t   g_poolUsed     = 0;
uint32_t   g_poolCapacity = 0;
SampleInfo g_samples[MAX_SAMPLES];
uint8_t    g_numSamples   = 0;

namespace {
alignas(4) uint32_t s_streamReservations = 0;

int findStreamSlotByName(const char* filename) {
    if (!filename || !filename[0]) return -1;
    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        const SamplerSlot& item = g_samplerSlotBank.slot(slot);
        if (item.mode != SAMPLER_SLOT_EMPTY &&
            strncmp(item.filename, filename, SAMPLE_NAME_LEN) == 0)
            return slot;
    }
    return -1;
}

bool validateStreamCandidate(const char* filename) {
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", DIR_SAMPLES, filename);
    File file;
    { SdIoGuard guard; file = SD.open(path, FILE_READ); }
    if (!file) return false;
    uint8_t header[WAV_PCM_HEADER_BYTES];
    WavMono16Info info = {};
    uint32_t fileBytes = 0;
    bool ok = false;
    {
        SdIoGuard guard;
        ok = file.seek(0) &&
            file.read(header, sizeof(header)) == static_cast<int>(sizeof(header));
        fileBytes = static_cast<uint32_t>(file.size());
        file.close();
    }
    ok = ok && wavParseCanonicalMono16Header(header, info) &&
         info.frames >= SAMPLER_SLICE_COUNT &&
         static_cast<uint64_t>(WAV_PCM_HEADER_BYTES) +
             static_cast<uint64_t>(info.frames) * sizeof(int16_t) <= fileBytes;
    if (!ok || info.sampleRate == 0) return false;
    const uint64_t scaled = static_cast<uint64_t>(info.frames) * SAMPLE_RATE;
    const uint32_t normalized = static_cast<uint32_t>(
        (scaled + info.sampleRate - 1u) / info.sampleRate);
    return normalized <= g_samplerSlotBank.quotaRemainingFrames();
}

int registerStreamFallback(const char* filename) {
    int streamSlot = findStreamSlotByName(filename);
    int8_t reservation = -1;
    if (streamSlot < 0) {
        if (!validateStreamCandidate(filename)) return -1;
        reservation = samplerReserveStreamReference();
        uint8_t decoded = 0;
        if (!samplerDecodeStreamReference(reservation, decoded) ||
            !streamingSamplerAssign(decoded, filename, SAMPLER_SLOT_MELODIC)) {
            samplerReleaseStreamReference(reservation);
            return -1;
        }
        streamSlot = decoded;
    }

    SampleInfo& sample = g_samples[g_numSamples];
    memset(&sample, 0, sizeof(sample));
    strncpy(sample.name, filename, SAMPLE_NAME_LEN - 1);
    sample.name[SAMPLE_NAME_LEN - 1] = 0;
    sample.streamSlot = static_cast<int8_t>(streamSlot);
    sample.used = true;
    return g_numSamples++;
}
}  // namespace

bool samplerInit() {
    // Directory availability must not depend on whether the optional legacy
    // RAM pool fits; streamed audio and projects use the same root.
    {
        SdIoGuard guard;
        if (!SD.exists(DIR_ROOT))       SD.mkdir(DIR_ROOT);
        if (!SD.exists(DIR_SAMPLES))    SD.mkdir(DIR_SAMPLES);
        if (!SD.exists(DIR_WAVETABLES)) SD.mkdir(DIR_WAVETABLES);
        if (!SD.exists(DIR_PROJECTS))   SD.mkdir(DIR_PROJECTS);
    }

    // Worker stacks and the wireless stacks are created after this call.
    // Preserve their boot reserve instead of letting the legacy RAM pool take
    // every currently-free byte before those subsystems start.
    constexpr uint32_t kBootReserveBytes = 112u * 1024u;
    const uint32_t freeBytes = heap_caps_get_free_size(
        MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    uint32_t bytes = freeBytes > kBootReserveBytes
        ? freeBytes - kBootReserveBytes : 0;
    if (bytes > SAMPLE_POOL_BYTES) bytes = SAMPLE_POOL_BYTES;
    bytes &= ~static_cast<uint32_t>(16u * 1024u - 1u);
    if (bytes < 32u * 1024u) {
        Serial.printf("RAM_SAMPLE_POOL unavailable free=%lu reserve=%lu\n",
                      static_cast<unsigned long>(freeBytes),
                      static_cast<unsigned long>(kBootReserveBytes));
        return false;
    }
    while (bytes >= 32 * 1024) {
        g_samplePool = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (g_samplePool) break;
        bytes -= 16 * 1024;
    }
    if (!g_samplePool) {
        Serial.printf("RAM_SAMPLE_POOL allocation_failed free=%lu reserve=%lu\n",
                      static_cast<unsigned long>(heap_caps_get_free_size(
                          MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
                      static_cast<unsigned long>(kBootReserveBytes));
        return false;
    }
    g_poolCapacity = bytes / sizeof(int16_t);
    Serial.printf("RAM_SAMPLE_POOL bytes=%lu free_after=%lu reserve=%lu\n",
                  static_cast<unsigned long>(bytes),
                  static_cast<unsigned long>(heap_caps_get_free_size(
                      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(kBootReserveBytes));
    samplerClearAll();
    return true;
}

void samplerClearAll() {
    g_poolUsed = 0;
    memset(g_samples, 0, sizeof(g_samples));
    for (uint8_t index = 0; index < MAX_SAMPLES; ++index)
        g_samples[index].streamSlot = -1;
    g_numSamples = 0;
    __atomic_store_n(&s_streamReservations, 0u, __ATOMIC_RELEASE);
}

int samplerFindByName(const char* filename) {
    for (int i = 0; i < g_numSamples; i++)
        if (g_samples[i].used && strncmp(g_samples[i].name, filename, SAMPLE_NAME_LEN) == 0)
            return i;
    return -1;
}

// ---------- WAV parsing ----------
static uint32_t rdU32(File& f) {
    uint8_t b[4]; { SdIoGuard guard; f.read(b, 4); }
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t rdU16(File& f) {
    uint8_t b[2]; { SdIoGuard guard; f.read(b, 2); }
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

bool wavDecodeToMono16(File& f, int16_t* dst, uint32_t maxFrames,
                       uint32_t& outFrames, uint32_t& outRate) {
    { SdIoGuard guard; f.seek(0); }
    char id[5] = {0};

    { SdIoGuard guard; f.read((uint8_t*)id, 4); }
    if (strncmp(id, "RIFF", 4) != 0) return false;
    rdU32(f);                              // riff size
    { SdIoGuard guard; f.read((uint8_t*)id, 4); }
    if (strncmp(id, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0, dataSize = 0, dataPos = 0;

    // walk chunks
    while ([&f]() { SdIoGuard guard; return f.available(); }() >= 8) {
        { SdIoGuard guard; f.read((uint8_t*)id, 4); }
        uint32_t sz = rdU32(f);
        if (strncmp(id, "fmt ", 4) == 0) {
            uint32_t start = [&f]() { SdIoGuard guard; return f.position(); }();
            fmt      = rdU16(f);
            channels = rdU16(f);
            rate     = rdU32(f);
            rdU32(f); rdU16(f);            // byte rate, block align
            bits     = rdU16(f);
            { SdIoGuard guard; f.seek(start + sz); }
        } else if (strncmp(id, "data", 4) == 0) {
            dataSize = sz;
            { SdIoGuard guard; dataPos = f.position(); f.seek(f.position() + sz); }
        } else {
            { SdIoGuard guard; f.seek(f.position() + sz + (sz & 1)); }
        }
    }

    if (fmt != 1 || dataPos == 0 || rate == 0) return false;              // PCM only
    if (bits != 16 && bits != 8) return false;
    if (channels < 1 || channels > 2) return false;

    uint32_t bytesPerFrame = channels * (bits / 8);
    uint32_t frames = dataSize / bytesPerFrame;
    if (frames > maxFrames) frames = maxFrames;

    { SdIoGuard guard; f.seek(dataPos); }
    static uint8_t chunk[512];
    uint32_t framesPerChunk = sizeof(chunk) / bytesPerFrame;
    uint32_t done = 0;

    while (done < frames) {
        uint32_t n = frames - done;
        if (n > framesPerChunk) n = framesPerChunk;
        int got = 0;
        { SdIoGuard guard; got = f.read(chunk, n * bytesPerFrame); }
        if (got <= 0) break;
        uint32_t gotFrames = (uint32_t)got / bytesPerFrame;

        for (uint32_t i = 0; i < gotFrames; i++) {
            int32_t v = 0;
            if (bits == 16) {
                const int16_t* p = (const int16_t*)(chunk + i * bytesPerFrame);
                v = (channels == 2) ? ((int32_t)p[0] + (int32_t)p[1]) / 2 : p[0];
            } else { // 8-bit unsigned
                const uint8_t* p = chunk + i * bytesPerFrame;
                int32_t a = ((int32_t)p[0] - 128) << 8;
                if (channels == 2) { int32_t b = ((int32_t)p[1] - 128) << 8; v = (a + b) / 2; }
                else v = a;
            }
            dst[done + i] = (int16_t)v;
        }
        done += gotFrames;
    }
    outFrames = done;
    outRate   = rate;
    return done > 0;
}

int samplerLoad(const char* filename) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return -1;
    int existing = samplerFindByName(filename);
    if (existing >= 0) return existing;
    if (g_numSamples >= MAX_SAMPLES) return -1;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", DIR_SAMPLES, filename);
    File f;
    { SdIoGuard guard; f = SD.open(path, FILE_READ); }
    if (!f) return -1;

    if (!g_samplePool || g_poolCapacity == 0) {
        { SdIoGuard guard; f.close(); }
        return registerStreamFallback(filename);
    }

    uint32_t freeFrames = g_poolCapacity - g_poolUsed;
    uint8_t canonicalHeader[WAV_PCM_HEADER_BYTES];
    WavMono16Info canonicalInfo = {};
    bool canonical = false;
    {
        SdIoGuard guard;
        canonical = f.seek(0) &&
            f.read(canonicalHeader, sizeof(canonicalHeader)) ==
                static_cast<int>(sizeof(canonicalHeader)) &&
            wavParseCanonicalMono16Header(canonicalHeader, canonicalInfo);
        f.seek(0);
    }
    if (canonical && canonicalInfo.frames > freeFrames) {
        { SdIoGuard guard; f.close(); }
        return registerStreamFallback(filename);
    }
    uint32_t frames = 0, rate = 0;
    bool ok = wavDecodeToMono16(f, g_samplePool + g_poolUsed, freeFrames, frames, rate);
    { SdIoGuard guard; f.close(); }
    if (!ok || frames == 0) return registerStreamFallback(filename);

    SampleInfo& s = g_samples[g_numSamples];
    strncpy(s.name, filename, SAMPLE_NAME_LEN - 1);
    s.name[SAMPLE_NAME_LEN - 1] = 0;
    s.offset = g_poolUsed;
    s.length = frames;
    s.rate   = rate;
    s.streamSlot = -1;
    s.used   = true;

    g_poolUsed += frames;
    return g_numSamples++;
}

int8_t samplerReserveStreamReference() {
    uint32_t reservations = __atomic_load_n(&s_streamReservations, __ATOMIC_ACQUIRE);
    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        const uint32_t bit = 1u << slot;
        if ((reservations & bit) != 0 ||
            g_samplerSlotBank.slot(slot).mode != SAMPLER_SLOT_EMPTY)
            continue;
        uint32_t expected = reservations;
        while ((expected & bit) == 0) {
            if (__atomic_compare_exchange_n(&s_streamReservations, &expected,
                                            expected | bit, false,
                                            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE))
                return sampleEncodeStreamReference(slot);
        }
        reservations = expected;
    }
    return -1;
}

int8_t samplerMakeStreamReference(uint8_t streamSlot) {
    return sampleEncodeStreamReference(streamSlot);
}

void samplerReleaseStreamReference(int8_t reference) {
    uint8_t streamSlot = 0;
    if (!samplerDecodeStreamReference(reference, streamSlot)) return;
    __atomic_fetch_and(&s_streamReservations, ~(1u << streamSlot), __ATOMIC_ACQ_REL);
}

bool samplerDecodeStreamReference(int reference, uint8_t& streamSlot) {
    return sampleDecodeStreamReference(reference, streamSlot);
}

bool samplerReferenceIsStreamed(int reference) {
    uint8_t streamSlot = 0;
    if (samplerDecodeStreamReference(reference, streamSlot)) return true;
    return reference >= 0 && reference < g_numSamples && g_samples[reference].used &&
           g_samples[reference].streamSlot >= 0;
}

bool samplerTriggerStreamedReference(int reference, float pitch, float volume) {
    uint8_t streamSlot = 0;
    if (!samplerDecodeStreamReference(reference, streamSlot)) {
        if (reference < 0 || reference >= g_numSamples ||
            !g_samples[reference].used || g_samples[reference].streamSlot < 0)
            return false;
        streamSlot = static_cast<uint8_t>(g_samples[reference].streamSlot);
    }
    SamplerLockEntry lock = {};
    lock.flags = SAMPLER_LOCK_PITCH | SAMPLER_LOCK_GAIN;
    float semitones = pitch > 0.0f ? 12.0f * log2f(pitch) : 0.0f;
    if (semitones < -24.0f) semitones = -24.0f;
    else if (semitones > 24.0f) semitones = 24.0f;
    if (volume < 0.0f) volume = 0.0f;
    else if (volume > 1.0f) volume = 1.0f;
    lock.pitchQ8 = static_cast<int16_t>(semitones * 256.0f);
    lock.gainQ15 = static_cast<uint16_t>(volume * 32767.0f);
    return streamingSamplerTrigger(streamSlot, 0, &lock);
}

const char* samplerReferenceName(int reference) {
    uint8_t streamSlot = 0;
    if (samplerDecodeStreamReference(reference, streamSlot)) {
        const SamplerSlot& item = g_samplerSlotBank.slot(streamSlot);
        return item.mode == SAMPLER_SLOT_EMPTY ? "" : item.filename;
    }
    if (reference < 0 || reference >= g_numSamples || !g_samples[reference].used)
        return "";
    return g_samples[reference].name;
}

SampleVoice g_previewVoice;   // zero-initialized: inactive
