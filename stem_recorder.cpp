#include "stem_recorder.h"

#include "config.h"
#include "master_recorder.h"
#include "mic_sampler.h"
#include "pcm_ring.h"
#include "sd_diagnostics.h"
#include "loop_engine.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

namespace {
constexpr uint32_t kRingFrames = 2048;
constexpr size_t kWriteFrames = 256;
constexpr const char* kTempPath = DIR_RECORDINGS "/.stems.tmp";

SpscRing<StemPcmFrame, kRingFrames> s_ring;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
StemRecorderSnapshot s_snapshot = {STEM_REC_UNAVAILABLE, "", 0, 0, 0, 0, 0};
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;

void setState(StemRecorderState state) {
    portENTER_CRITICAL(&s_mux);
    s_snapshot.state = state;
    portEXIT_CRITICAL(&s_mux);
}

StemRecorderState getState() {
    portENTER_CRITICAL(&s_mux);
    const StemRecorderState state = s_snapshot.state;
    portEXIT_CRITICAL(&s_mux);
    return state;
}

void addError() {
    portENTER_CRITICAL(&s_mux);
    ++s_snapshot.errors;
    portEXIT_CRITICAL(&s_mux);
}

bool selectPath(const char* prefix, const char* extension, char* output, size_t length) {
    if (!SD.exists(DIR_ROOT) && !SD.mkdir(DIR_ROOT)) return false;
    if (!SD.exists(DIR_RECORDINGS) && !SD.mkdir(DIR_RECORDINGS)) return false;
    for (uint16_t number = 1; number <= 999; ++number) {
        snprintf(output, length, "%s/%s%03u.%s", DIR_RECORDINGS, prefix,
                 static_cast<unsigned>(number), extension);
        if (!SD.exists(output)) return true;
    }
    return false;
}

void recoverInterruptedStems() {
    if (!SD.exists(kTempPath)) return;
    File file = SD.open(kTempPath, "r+");
    if (!file) { addError(); return; }
    const uint32_t bytes = static_cast<uint32_t>(file.size());
    const uint32_t payload = bytes > STEM_FILE_HEADER_BYTES ? bytes - STEM_FILE_HEADER_BYTES : 0;
    const uint32_t frames = payload / sizeof(StemPcmFrame);
    const bool recoverable = frames > 0;
    char recovered[64];
    bool ok = selectPath("STEMREC", recoverable ? "mss" : "bad", recovered,
                         sizeof(recovered));
    if (ok && recoverable) {
        uint8_t header[STEM_FILE_HEADER_BYTES];
        stemBuildHeader(header, SAMPLE_RATE, frames);
        ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
        if (ok) file.flush();
    }
    file.close();
    if (ok) ok = SD.rename(kTempPath, recovered);
    if (!ok) addError();
    Serial.printf("STEM_RECOVERY state=%s path=%s frames=%lu trailing=%lu\n",
                  ok ? (recoverable ? "RECOVERED" : "QUARANTINED") : "FAILED",
                  ok ? recovered : "-", static_cast<unsigned long>(frames),
                  static_cast<unsigned long>(payload % sizeof(StemPcmFrame)));
}

void stemTask(void*) {
    StemPcmFrame frames[kWriteFrames];
    uint8_t header[STEM_FILE_HEADER_BYTES];
    stemBuildHeader(header, SAMPLE_RATE, 0);

    File file = SD.open(kTempPath, FILE_WRITE);
    bool ok = file && file.write(header, sizeof(header)) == sizeof(header);
    if (!ok) addError();
    if (ok && getState() == STEM_REC_STARTING) setState(STEM_REC_RECORDING);

    while (ok) {
        const size_t available = s_ring.pop(frames, kWriteFrames);
        if (available) {
            const size_t bytes = available * sizeof(StemPcmFrame);
            const uint32_t started = micros();
            ok = file.write(reinterpret_cast<uint8_t*>(frames), bytes) == bytes;
            const uint32_t elapsed = micros() - started;
            portENTER_CRITICAL(&s_mux);
            if (ok) s_snapshot.framesWritten += static_cast<uint32_t>(available);
            else ++s_snapshot.errors;
            if (elapsed > s_snapshot.maxWriteUs) s_snapshot.maxWriteUs = elapsed;
            portEXIT_CRITICAL(&s_mux);
        } else {
            const StemRecorderState state = getState();
            if (state == STEM_REC_STOPPING) break;
            if (state != STEM_REC_RECORDING && state != STEM_REC_STARTING) { ok = false; break; }
            vTaskDelay(1);
        }
    }

    if (ok) {
        const StemRecorderSnapshot beforeFinalize = stemRecorderSnapshot();
        stemBuildHeader(header, SAMPLE_RATE, beforeFinalize.framesWritten);
        const uint32_t started = micros();
        ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
        if (ok) file.flush();
        const uint32_t elapsed = micros() - started;
        portENTER_CRITICAL(&s_mux);
        if (elapsed > s_snapshot.maxWriteUs) s_snapshot.maxWriteUs = elapsed;
        if (!ok) ++s_snapshot.errors;
        portEXIT_CRITICAL(&s_mux);
    }
    if (file) file.close();

    if (ok) {
        const StemRecorderSnapshot finalizing = stemRecorderSnapshot();
        ok = SD.rename(kTempPath, finalizing.path);
        if (!ok) addError();
    }
    setState(ok ? STEM_REC_COMPLETE : STEM_REC_ERROR);

    const StemRecorderSnapshot done = stemRecorderSnapshot();
    Serial.printf("STEMS state=%s path=%s frames=%lu dropped=%lu highWater=%lu "
                  "maxWrite=%luus errors=%lu\n",
                  stemRecorderStateName(done.state), done.path,
                  static_cast<unsigned long>(done.framesWritten),
                  static_cast<unsigned long>(done.droppedFrames),
                  static_cast<unsigned long>(done.ringHighWater),
                  static_cast<unsigned long>(done.maxWriteUs),
                  static_cast<unsigned long>(done.errors));

    portENTER_CRITICAL(&s_mux);
    s_task = nullptr;
    portEXIT_CRITICAL(&s_mux);
    vTaskDelete(nullptr);
}
}  // namespace

void stemRecorderInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    s_ring.reset();
    portENTER_CRITICAL(&s_mux);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = sdMounted ? STEM_REC_IDLE : STEM_REC_UNAVAILABLE;
    s_snapshot.errors = sdMounted ? 0 : 1;
    s_task = nullptr;
    portEXIT_CRITICAL(&s_mux);
    if (sdMounted) recoverInterruptedStems();
}

bool stemRecorderStart() {
    if (!s_sdMounted || stemRecorderIsBusy() || masterRecorderIsBusy() ||
        sdDiagnosticsIsRunning() || micRecActive() || loopEngineIsRecording() ||
        SD.exists(kTempPath)) return false;
    char path[64];
    if (!selectPath("STEM", "mss", path, sizeof(path))) return false;
    s_ring.reset();
    portENTER_CRITICAL(&s_mux);
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.state = STEM_REC_STARTING;
    snprintf(s_snapshot.path, sizeof(s_snapshot.path), "%s", path);
    portEXIT_CRITICAL(&s_mux);
    if (xTaskCreatePinnedToCore(stemTask, "stem_file", 7168, nullptr, 1, &s_task, 1) == pdPASS)
        return true;
    addError();
    setState(STEM_REC_ERROR);
    return false;
}

bool stemRecorderStop() {
    const StemRecorderState state = getState();
    if (state != STEM_REC_STARTING && state != STEM_REC_RECORDING) return false;
    setState(STEM_REC_STOPPING);
    return true;
}

bool stemRecorderIsBusy() {
    const StemRecorderState state = getState();
    portENTER_CRITICAL(&s_mux);
    const bool taskRunning = s_task != nullptr;
    portEXIT_CRITICAL(&s_mux);
    return taskRunning || state == STEM_REC_STARTING || state == STEM_REC_RECORDING ||
           state == STEM_REC_STOPPING;
}

void stemRecorderPush(const StemPcmFrame* frames, size_t count) {
    if (!frames || !count || getState() != STEM_REC_RECORDING) return;
    const size_t pushed = s_ring.push(frames, count);
    const uint32_t highWater = s_ring.size();
    portENTER_CRITICAL(&s_mux);
    if (pushed < count) s_snapshot.droppedFrames += static_cast<uint32_t>(count - pushed);
    if (highWater > s_snapshot.ringHighWater) s_snapshot.ringHighWater = highWater;
    portEXIT_CRITICAL(&s_mux);
}

StemRecorderSnapshot stemRecorderSnapshot() {
    portENTER_CRITICAL(&s_mux);
    const StemRecorderSnapshot copy = s_snapshot;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

const char* stemRecorderStateName(StemRecorderState state) {
    switch (state) {
        case STEM_REC_UNAVAILABLE: return "unavailable";
        case STEM_REC_IDLE: return "idle";
        case STEM_REC_STARTING: return "starting";
        case STEM_REC_RECORDING: return "recording";
        case STEM_REC_STOPPING: return "stopping";
        case STEM_REC_COMPLETE: return "complete";
        case STEM_REC_ERROR: return "error";
        default: return "unknown";
    }
}
