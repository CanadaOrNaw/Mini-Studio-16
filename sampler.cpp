// ============================================================
// CardputerGroovebox - sampler.cpp
// ============================================================
#include "sampler.h"
#include <SD.h>
#include <string.h>
#include <stdio.h>
#include <esp_heap_caps.h>

int16_t*   g_samplePool   = nullptr;
uint32_t   g_poolUsed     = 0;
uint32_t   g_poolCapacity = 0;
SampleInfo g_samples[MAX_SAMPLES];
uint8_t    g_numSamples   = 0;

bool samplerInit() {
    // Allocate the pool once; shrink until it fits in free internal RAM.
    uint32_t bytes = SAMPLE_POOL_BYTES;
    while (bytes >= 32 * 1024) {
        g_samplePool = (int16_t*)heap_caps_malloc(bytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
        if (g_samplePool) break;
        bytes -= 16 * 1024;
    }
    if (!g_samplePool) return false;
    g_poolCapacity = bytes / sizeof(int16_t);
    samplerClearAll();

    if (!SD.exists(DIR_ROOT))       SD.mkdir(DIR_ROOT);
    if (!SD.exists(DIR_SAMPLES))    SD.mkdir(DIR_SAMPLES);
    if (!SD.exists(DIR_WAVETABLES)) SD.mkdir(DIR_WAVETABLES);
    if (!SD.exists(DIR_PROJECTS))   SD.mkdir(DIR_PROJECTS);
    return true;
}

void samplerClearAll() {
    g_poolUsed = 0;
    memset(g_samples, 0, sizeof(g_samples));
    g_numSamples = 0;
}

int samplerFindByName(const char* filename) {
    for (int i = 0; i < g_numSamples; i++)
        if (g_samples[i].used && strncmp(g_samples[i].name, filename, SAMPLE_NAME_LEN) == 0)
            return i;
    return -1;
}

// ---------- WAV parsing ----------
static uint32_t rdU32(File& f) {
    uint8_t b[4]; f.read(b, 4);
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
static uint16_t rdU16(File& f) {
    uint8_t b[2]; f.read(b, 2);
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

bool wavDecodeToMono16(File& f, int16_t* dst, uint32_t maxFrames,
                       uint32_t& outFrames, uint32_t& outRate) {
    f.seek(0);
    char id[5] = {0};

    f.read((uint8_t*)id, 4);
    if (strncmp(id, "RIFF", 4) != 0) return false;
    rdU32(f);                              // riff size
    f.read((uint8_t*)id, 4);
    if (strncmp(id, "WAVE", 4) != 0) return false;

    uint16_t fmt = 0, channels = 0, bits = 0;
    uint32_t rate = 0, dataSize = 0, dataPos = 0;

    // walk chunks
    while (f.available() >= 8) {
        f.read((uint8_t*)id, 4);
        uint32_t sz = rdU32(f);
        if (strncmp(id, "fmt ", 4) == 0) {
            uint32_t start = f.position();
            fmt      = rdU16(f);
            channels = rdU16(f);
            rate     = rdU32(f);
            rdU32(f); rdU16(f);            // byte rate, block align
            bits     = rdU16(f);
            f.seek(start + sz);
        } else if (strncmp(id, "data", 4) == 0) {
            dataSize = sz;
            dataPos  = f.position();
            f.seek(f.position() + sz);
        } else {
            f.seek(f.position() + sz + (sz & 1));
        }
    }

    if (fmt != 1 || dataPos == 0 || rate == 0) return false;              // PCM only
    if (bits != 16 && bits != 8) return false;
    if (channels < 1 || channels > 2) return false;

    uint32_t bytesPerFrame = channels * (bits / 8);
    uint32_t frames = dataSize / bytesPerFrame;
    if (frames > maxFrames) frames = maxFrames;

    f.seek(dataPos);
    static uint8_t chunk[512];
    uint32_t framesPerChunk = sizeof(chunk) / bytesPerFrame;
    uint32_t done = 0;

    while (done < frames) {
        uint32_t n = frames - done;
        if (n > framesPerChunk) n = framesPerChunk;
        int got = f.read(chunk, n * bytesPerFrame);
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
    int existing = samplerFindByName(filename);
    if (existing >= 0) return existing;
    if (g_numSamples >= MAX_SAMPLES) return -1;

    char path[80];
    snprintf(path, sizeof(path), "%s/%s", DIR_SAMPLES, filename);
    File f = SD.open(path, FILE_READ);
    if (!f) return -1;

    uint32_t freeFrames = g_poolCapacity - g_poolUsed;
    uint32_t frames = 0, rate = 0;
    bool ok = wavDecodeToMono16(f, g_samplePool + g_poolUsed, freeFrames, frames, rate);
    f.close();
    if (!ok || frames == 0) return -1;

    SampleInfo& s = g_samples[g_numSamples];
    strncpy(s.name, filename, SAMPLE_NAME_LEN - 1);
    s.name[SAMPLE_NAME_LEN - 1] = 0;
    s.offset = g_poolUsed;
    s.length = frames;
    s.rate   = rate;
    s.used   = true;

    g_poolUsed += frames;
    return g_numSamples++;
}

SampleVoice g_previewVoice;   // zero-initialized: inactive
