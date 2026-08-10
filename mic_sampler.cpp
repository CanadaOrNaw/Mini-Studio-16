// ============================================================
// Microgroove [BRANCH: live-sampling] - mic_sampler.cpp
// ============================================================
#include "mic_sampler.h"
#include "sampler.h"
#include "sequencer.h"
#include "ui.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "sd_diagnostics.h"
#include "streaming_sampler.h"
#include "sd_io_arbiter.h"
#include <M5Cardputer.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>

int16_t*          g_scratch    = nullptr;
volatile uint32_t g_rsmpRemain = 0;
volatile uint32_t g_scratchWr  = 0;

static bool     s_recActive   = false;
static bool     s_streamingMic = false;
static uint8_t  s_recLane     = 0;
static bool     s_rsmpPending = false;
static uint32_t s_rsmpFrames  = 0;
static uint8_t  s_micCount    = 0;      // MIC01.. name counter
static uint8_t  s_rsmpCount   = 0;

// ping-pong chunks queued into M5Unified's mic driver
#define CHUNK 256
static int16_t  s_chunk[2][CHUNK];
static int      s_curChunk = 0;
static float    s_level    = 0.0f;

bool micSamplerInit() {
    g_scratch = (int16_t*)heap_caps_malloc(SCRATCH_FRAMES * sizeof(int16_t),
                                           MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    return g_scratch != nullptr;
}

// ---------- auto trim ----------
static void trimScratch(uint32_t total, uint32_t rate,
                        uint32_t& start, uint32_t& len) {
    uint32_t pre = (rate * TRIM_PREROLL_MS) / 1000;
    uint32_t a = 0, b = total;
    while (a < total && (uint16_t)abs(g_scratch[a]) < TRIM_THRESHOLD) a++;
    while (b > a     && (uint16_t)abs(g_scratch[b - 1]) < TRIM_THRESHOLD) b--;
    if (a >= b) { start = 0; len = 0; return; }          // silence
    start = (a > pre) ? a - pre : 0;
    len   = b - start;
}

// ---------- WAV write + pool commit ----------
static bool writeWav(const char* name, uint32_t start, uint32_t frames, uint32_t rate) {
    if (masterRecorderIsBusy() || stemRecorderIsBusy()) return false;
    char path[80];
    snprintf(path, sizeof(path), "%s/%s", DIR_SAMPLES, name);
    File f;
    { SdIoGuard guard; SD.remove(path); f = SD.open(path, FILE_WRITE); }
    if (!f) return false;
    uint32_t dataBytes = frames * 2;
    uint8_t h[44] = {'R','I','F','F',0,0,0,0,'W','A','V','E','f','m','t',' ',
                     16,0,0,0, 1,0, 1,0, 0,0,0,0, 0,0,0,0, 2,0, 16,0,
                     'd','a','t','a',0,0,0,0};
    uint32_t riff = 36 + dataBytes, brate = rate * 2;
    memcpy(h + 4,  &riff,  4);
    memcpy(h + 24, &rate,  4);
    memcpy(h + 28, &brate, 4);
    memcpy(h + 40, &dataBytes, 4);
    bool ok = false;
    { SdIoGuard guard; ok = f.write(h, 44) == 44; }
    uint32_t written = 0;
    while (ok && written < dataBytes) {
        const uint32_t remaining = dataBytes - written;
        const uint32_t chunk = remaining < 4096u ? remaining : 4096u;
        size_t got = 0;
        { SdIoGuard guard;
          got = f.write(reinterpret_cast<uint8_t*>(g_scratch + start) + written, chunk); }
        if (got != chunk) ok = false;
        else written += chunk;
    }
    { SdIoGuard guard; f.close(); }
    return ok;
}

// commit scratch[start..start+frames] to a lane; SD-first, RAM fallback
static bool commitToLane(uint8_t lane, const char* base, uint8_t& counter,
                         uint32_t start, uint32_t frames, uint32_t rate) {
    if (frames < 64) { uiStatus("TOO QUIET"); return false; }
    char name[SAMPLE_NAME_LEN];
    snprintf(name, sizeof(name), "%s%02u.wav", base, (unsigned)(++counter));

    int slot = -1;
    if (writeWav(name, start, frames, rate)) {
        slot = samplerLoad(name);                       // reuses pool logic
    }
    if (slot < 0) {                                     // RAM-only fallback
        if (g_numSamples >= MAX_SAMPLES ||
            g_poolUsed + frames > g_poolCapacity) { uiStatus("POOL FULL"); return false; }
        memcpy(g_samplePool + g_poolUsed, g_scratch + start, frames * 2);
        SampleInfo& s = g_samples[g_numSamples];
        strncpy(s.name, name, SAMPLE_NAME_LEN - 1);
        s.name[SAMPLE_NAME_LEN - 1] = 0;
        s.offset = g_poolUsed; s.length = frames; s.rate = rate; s.used = true;
        g_poolUsed += frames;
        slot = g_numSamples++;
    }
    DrumLane& d = g_drumLanes[lane];
    d.engine = ENG_SMPL; d.sampleSlot = (int8_t)slot; d.smp.init();
    d.smp.trigger(slot, 1.0f, d.volume);                // instant audition
    return true;
}

// ---------- mic recording ----------
bool micRecStart(uint8_t lane) {
    if (s_recActive || !g_scratch || masterRecorderIsBusy() || stemRecorderIsBusy() ||
        sdDiagnosticsIsRunning() || loopEngineIsRecording()) return false;
    sequencerStop();
    M5Cardputer.Speaker.end();          // ES8311: avoid duplex contention (verify on hw)
    M5Cardputer.Mic.begin();
    s_recLane = lane; s_recActive = true; s_streamingMic = false;
    g_scratchWr = 0; s_curChunk = 0; s_level = 0;
    M5Cardputer.Mic.record(s_chunk[0], CHUNK, MIC_RATE);
    M5Cardputer.Mic.record(s_chunk[1], CHUNK, MIC_RATE);
    uiStatus("SAMPLING...");
    return true;
}

bool micStreamRecStart(uint8_t slot, SamplerSlotMode mode) {
    if (s_recActive || masterRecorderIsBusy() || stemRecorderIsBusy() ||
        sdDiagnosticsIsRunning() || loopEngineIsRecording() ||
        !streamingSamplerBeginRecord(slot, mode, MIC_RATE, STREAM_SAMPLE_INPUT_MIC))
        return false;
    sequencerStop();
    M5Cardputer.Speaker.end();
    M5Cardputer.Mic.begin();
    s_recLane = 0;
    s_recActive = true;
    s_streamingMic = true;
    s_curChunk = 0;
    s_level = 0;
    M5Cardputer.Mic.record(s_chunk[0], CHUNK, MIC_RATE);
    M5Cardputer.Mic.record(s_chunk[1], CHUNK, MIC_RATE);
    uiStatus("STREAM MIC...");
    return true;
}

void micSamplerUpdate() {
    if (s_recActive) {
        // a chunk finished when the driver moved to the queued one
        while (!M5Cardputer.Mic.isRecording() ||
               M5Cardputer.Mic.isRecording() == 1 /*one buf left*/) {
            int16_t* done = s_chunk[s_curChunk];
            uint32_t room = s_streamingMic ? CHUNK : SCRATCH_FRAMES - g_scratchWr;
            uint32_t n = (room < CHUNK) ? room : CHUNK;
            if (n) {
                if (s_streamingMic) {
                    streamingSamplerRecordPush(STREAM_SAMPLE_INPUT_MIC, done, n);
                } else {
                    memcpy(g_scratch + g_scratchWr, done, n * 2);
                    g_scratchWr += n;
                }
                int16_t pk = 0;
                for (uint32_t i = 0; i < n; i++) { int16_t a = abs(done[i]); if (a > pk) pk = a; }
                s_level = s_level * 0.6f + (pk / 32768.0f) * 0.4f;
            }
            if ((!s_streamingMic && g_scratchWr >= SCRATCH_FRAMES) ||
                (s_streamingMic &&
                 streamingSamplerSnapshot().recordState == STREAM_SAMPLE_REC_STOPPING)) {
                micRecStop();
                return;
            }
            M5Cardputer.Mic.record(done, CHUNK, MIC_RATE);   // re-queue
            s_curChunk ^= 1;
            if (M5Cardputer.Mic.isRecording() >= 2) break;
        }
        // level meter via the hold-progress footer bar
        g_holdProg = s_level > 1.0f ? 1.0f : s_level;
        strncpy(g_holdLabel, "SAMPLING", sizeof(g_holdLabel));
        static uint32_t t = 0;
        if (millis() - t > 50) { t = millis(); g_needRedraw = true; }
    }
    if (s_rsmpFrames && g_rsmpRemain == 0) {             // resample finished
        s_rsmpFrames = 0; s_rsmpPending = true;
        uiStatus("RSMP: TAP A PAD");
        g_needRedraw = true;
    }
}

void micRecStop() {
    if (!s_recActive) return;
    s_recActive = false;
    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    g_holdProg = 0; g_holdLabel[0] = 0;

    if (s_streamingMic) {
        s_streamingMic = false;
        streamingSamplerStopRecord();
        uiStatus("SAMPLE SAVING...");
        g_needRedraw = true;
        return;
    }

    uint32_t start, len;
    trimScratch(g_scratchWr, MIC_RATE, start, len);
    if (commitToLane(s_recLane, "MIC", s_micCount, start, len, MIC_RATE))
        uiStatus("SAMPLED!");
    g_needRedraw = true;
}

bool micRecActive() { return s_recActive; }

// ---------- resampling (engine mix -> scratch, tap in audio task) ----------
void resampleArm() {
    if (s_recActive || !g_scratch || g_rsmpRemain) return;
    g_scratchWr = 0;
    s_rsmpFrames = SCRATCH_FRAMES;
    g_rsmpRemain = SCRATCH_FRAMES;      // audio task decrements
    uiStatus("RESAMPLING...");
}

bool resamplePending() { return s_rsmpPending; }

void resampleCommit(uint8_t lane) {
    s_rsmpPending = false;
    uint32_t start, len;
    trimScratch(g_scratchWr, SAMPLE_RATE, start, len);
    if (commitToLane(lane, "RSM", s_rsmpCount, start, len, SAMPLE_RATE))
        uiStatus("RESAMPLED!");
    g_needRedraw = true;
}
