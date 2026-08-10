#include "master_recorder.h"
#include "master_recorder_session.h"

#include "config.h"
#include "pcm_ring.h"
#include "sd_diagnostics.h"
#include "mic_sampler.h"
#include "wav_file.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "sd_io_arbiter.h"
#include "streaming_sampler.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

namespace {
constexpr uint32_t kRingFrames = 4096;
constexpr size_t kWriteFrames = 2048;
constexpr const char* kTempPath = DIR_RECORDINGS "/.master.tmp";

SpscRing<int16_t, kRingFrames> s_ring;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
MasterRecorderSession s_session;
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;

MasterRecorderState getState() {
    portENTER_CRITICAL(&s_mux);
    const MasterRecorderState state = s_session.state();
    portEXIT_CRITICAL(&s_mux);
    return state;
}

void addError() {
    portENTER_CRITICAL(&s_mux);
    s_session.noteError();
    portEXIT_CRITICAL(&s_mux);
}

void noteWrite(size_t frames, uint32_t elapsedUs) {
    portENTER_CRITICAL(&s_mux);
    s_session.noteWrite(static_cast<uint32_t>(frames), elapsedUs);
    portEXIT_CRITICAL(&s_mux);
}

bool selectAvailablePath(const char* prefix, const char* extension, char* output, size_t length) {
    SdIoGuard guard;
    if (!SD.exists(DIR_ROOT) && !SD.mkdir(DIR_ROOT)) return false;
    if (!SD.exists(DIR_RECORDINGS) && !SD.mkdir(DIR_RECORDINGS)) return false;

    char candidate[64];
    for (uint16_t number = 1; number <= 999; ++number) {
        snprintf(candidate, sizeof(candidate), "%s/%s%03u.%s", DIR_RECORDINGS, prefix,
                 static_cast<unsigned>(number), extension);
        if (!SD.exists(candidate)) {
            strncpy(output, candidate, length - 1);
            output[length - 1] = 0;
            return true;
        }
    }
    return false;
}

void failRecorder(File& file) {
    addError();
    if (file) { SdIoGuard guard; file.close(); }
    portENTER_CRITICAL(&s_mux);
    s_session.fail();
    portEXIT_CRITICAL(&s_mux);
}

void recoverInterruptedRecording() {
    { SdIoGuard guard; if (!SD.exists(kTempPath)) return; }

    File file;
    { SdIoGuard guard; file = SD.open(kTempPath, "r+"); }
    if (!file) { addError(); return; }
    const WavRecoveryPlan plan = wavPlanMono16Recovery(
        [&file]() { SdIoGuard guard; return static_cast<uint32_t>(file.size()); }());
    char recovered[64];
    const bool havePath = selectAvailablePath("RECOVER", plan.recoverable ? "wav" : "bad",
                                               recovered, sizeof(recovered));
    bool ok = havePath;
    if (ok && plan.recoverable) {
        uint8_t header[WAV_PCM_HEADER_BYTES];
        wavBuildMono16Header(header, SAMPLE_RATE, plan.frames);
        SdIoGuard guard;
        ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
        if (ok) file.flush();
    }
    { SdIoGuard guard; file.close(); if (ok) ok = SD.rename(kTempPath, recovered); }

    portENTER_CRITICAL(&s_mux);
    if (ok && plan.recoverable) s_session.noteRecovery(recovered, plan.frames);
    else if (!ok) s_session.noteError();
    portEXIT_CRITICAL(&s_mux);

    Serial.printf("MASTER_RECOVERY state=%s path=%s frames=%lu trailing=%u\n",
                  ok ? (plan.recoverable ? "RECOVERED" : "QUARANTINED") : "FAILED",
                  havePath ? recovered : "-", static_cast<unsigned long>(plan.frames),
                  static_cast<unsigned>(plan.ignoredTrailingBytes));
}

void recorderTask(void*) {
    int16_t block[kWriteFrames];
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, SAMPLE_RATE, 0);

    File file;
    bool opened = false;
    { SdIoGuard guard; file = SD.open(kTempPath, FILE_WRITE);
      opened = file && file.write(header, sizeof(header)) == sizeof(header); }
    if (!opened) {
        failRecorder(file);
    } else {
        portENTER_CRITICAL(&s_mux);
        s_session.markRecording();
        portEXIT_CRITICAL(&s_mux);

        bool ok = true;
        while (ok) {
            const size_t available = s_ring.pop(block, kWriteFrames);
            if (available > 0) {
                const uint32_t started = micros();
                const size_t bytes = available * sizeof(int16_t);
                bool wrote = false;
                { SdIoGuard guard; wrote = file.write(
                    reinterpret_cast<const uint8_t*>(block), bytes) == bytes; }
                if (!wrote) {
                    addError();
                    ok = false;
                    break;
                }
                noteWrite(available, micros() - started);
            } else {
                const MasterRecorderState state = getState();
                if (state == MASTER_REC_STOPPING) break;
                if (state != MASTER_REC_RECORDING && state != MASTER_REC_STARTING) {
                    ok = false;
                    break;
                }
                vTaskDelay(1);
            }
        }

        if (ok) {
            const MasterRecorderSnapshot beforeFinalize = masterRecorderSnapshot();
            wavBuildMono16Header(header, SAMPLE_RATE, beforeFinalize.framesWritten);
            const uint32_t started = micros();
            SdIoGuard guard;
            ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
            if (ok) file.flush();
            noteWrite(0, micros() - started);
        }
        { SdIoGuard guard; file.close(); }

        if (ok) {
            const MasterRecorderSnapshot finalizing = masterRecorderSnapshot();
            SdIoGuard guard;
            if (!SD.rename(kTempPath, finalizing.path)) {
                addError();
                ok = false;
            }
        }
        portENTER_CRITICAL(&s_mux);
        if (ok) s_session.complete(); else s_session.fail();
        portEXIT_CRITICAL(&s_mux);
    }

    const MasterRecorderSnapshot done = masterRecorderSnapshot();
    Serial.printf(
        "MASTER state=%s path=%s frames=%lu dropped=%lu highWater=%lu "
        "maxWrite=%luus errors=%lu\n",
        masterRecorderStateName(done.state), done.path,
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

void masterRecorderInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    s_ring.reset();
    portENTER_CRITICAL(&s_mux);
    s_session.initialize(sdMounted);
    s_task = nullptr;
    portEXIT_CRITICAL(&s_mux);
    if (sdMounted) recoverInterruptedRecording();
}

bool masterRecorderStart() {
    if (!s_sdMounted || masterRecorderIsBusy() || stemRecorderIsBusy() ||
        sdDiagnosticsIsRunning() || micRecActive() || loopEngineIsRecording() ||
        streamingSamplerIsRecording())
        return false;
    { SdIoGuard guard; if (SD.exists(kTempPath)) return false; }
    char path[64];
    if (!selectAvailablePath("MASTER", "wav", path, sizeof(path))) return false;

    s_ring.reset();
    portENTER_CRITICAL(&s_mux);
    s_session.begin(path);
    portEXIT_CRITICAL(&s_mux);

    // The worker keeps a 4 KiB sequential-write block on its stack. Leave
    // ample room for FatFS/File calls and formatted completion diagnostics.
    if (xTaskCreatePinnedToCore(recorderTask, "master_wav", 8192, nullptr, 1,
                                &s_task, 1) == pdPASS)
        return true;

    addError();
    portENTER_CRITICAL(&s_mux);
    s_session.fail();
    portEXIT_CRITICAL(&s_mux);
    return false;
}

bool masterRecorderStop() {
    portENTER_CRITICAL(&s_mux);
    const bool stopped = s_session.requestStop();
    portEXIT_CRITICAL(&s_mux);
    return stopped;
}

bool masterRecorderIsBusy() {
    const MasterRecorderState state = getState();
    portENTER_CRITICAL(&s_mux);
    const bool taskRunning = s_task != nullptr;
    portEXIT_CRITICAL(&s_mux);
    return taskRunning || state == MASTER_REC_STARTING || state == MASTER_REC_RECORDING ||
           state == MASTER_REC_STOPPING;
}

void masterRecorderPush(const int16_t* frames, size_t count) {
    if (!frames || count == 0 || getState() != MASTER_REC_RECORDING) return;
    const size_t pushed = s_ring.push(frames, count);
    const uint32_t highWater = s_ring.size();
    portENTER_CRITICAL(&s_mux);
    s_session.noteProduced(static_cast<uint32_t>(count), static_cast<uint32_t>(pushed),
                           highWater);
    portEXIT_CRITICAL(&s_mux);
}

MasterRecorderSnapshot masterRecorderSnapshot() {
    portENTER_CRITICAL(&s_mux);
    const MasterRecorderSnapshot copy = s_session.snapshot();
    portEXIT_CRITICAL(&s_mux);
    return copy;
}

const char* masterRecorderStateName(MasterRecorderState state) {
    switch (state) {
        case MASTER_REC_UNAVAILABLE: return "unavailable";
        case MASTER_REC_IDLE: return "idle";
        case MASTER_REC_STARTING: return "starting";
        case MASTER_REC_RECORDING: return "recording";
        case MASTER_REC_STOPPING: return "stopping";
        case MASTER_REC_COMPLETE: return "complete";
        case MASTER_REC_ERROR: return "error";
        default: return "unknown";
    }
}
