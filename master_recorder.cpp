#include "master_recorder.h"

#include "config.h"
#include "pcm_ring.h"
#include "sd_diagnostics.h"
#include "mic_sampler.h"
#include "wav_file.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

namespace {
constexpr uint32_t kRingFrames = 8192;
constexpr size_t kWriteFrames = 2048;
constexpr const char* kTempPath = DIR_RECORDINGS "/.master.tmp";

SpscRing<int16_t, kRingFrames> s_ring;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
MasterRecorderSnapshot s_snapshot = {MASTER_REC_UNAVAILABLE, "", 0, 0, 0, 0, 0};
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;

void setState(MasterRecorderState state) {
    portENTER_CRITICAL(&s_mux);
    s_snapshot.state = state;
    portEXIT_CRITICAL(&s_mux);
}

MasterRecorderState getState() {
    portENTER_CRITICAL(&s_mux);
    const MasterRecorderState state = s_snapshot.state;
    portEXIT_CRITICAL(&s_mux);
    return state;
}

void addError() {
    portENTER_CRITICAL(&s_mux);
    ++s_snapshot.errors;
    portEXIT_CRITICAL(&s_mux);
}

void noteWrite(size_t frames, uint32_t elapsedUs) {
    portENTER_CRITICAL(&s_mux);
    s_snapshot.framesWritten += static_cast<uint32_t>(frames);
    if (elapsedUs > s_snapshot.maxWriteUs) s_snapshot.maxWriteUs = elapsedUs;
    portEXIT_CRITICAL(&s_mux);
}

bool selectOutputPath() {
    if (!SD.exists(DIR_ROOT) && !SD.mkdir(DIR_ROOT)) return false;
    if (!SD.exists(DIR_RECORDINGS) && !SD.mkdir(DIR_RECORDINGS)) return false;

    char candidate[64];
    for (uint16_t number = 1; number <= 999; ++number) {
        snprintf(candidate, sizeof(candidate), DIR_RECORDINGS "/MASTER%03u.wav",
                 static_cast<unsigned>(number));
        if (!SD.exists(candidate)) {
            portENTER_CRITICAL(&s_mux);
            strncpy(s_snapshot.path, candidate, sizeof(s_snapshot.path) - 1);
            s_snapshot.path[sizeof(s_snapshot.path) - 1] = 0;
            portEXIT_CRITICAL(&s_mux);
            return true;
        }
    }
    return false;
}

void failRecorder(File& file) {
    addError();
    if (file) file.close();
    setState(MASTER_REC_ERROR);
}

void recorderTask(void*) {
    int16_t block[kWriteFrames];
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, SAMPLE_RATE, 0);

    SD.remove(kTempPath);
    File file = SD.open(kTempPath, FILE_WRITE);
    if (!file || file.write(header, sizeof(header)) != sizeof(header)) {
        failRecorder(file);
    } else {
        if (getState() == MASTER_REC_STARTING) setState(MASTER_REC_RECORDING);

        bool ok = true;
        while (ok) {
            const size_t available = s_ring.pop(block, kWriteFrames);
            if (available > 0) {
                const uint32_t started = micros();
                const size_t bytes = available * sizeof(int16_t);
                if (file.write(reinterpret_cast<const uint8_t*>(block), bytes) != bytes) {
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
            ok = file.seek(0) && file.write(header, sizeof(header)) == sizeof(header);
            if (ok) file.flush();
            noteWrite(0, micros() - started);
        }
        file.close();

        if (ok) {
            const MasterRecorderSnapshot finalizing = masterRecorderSnapshot();
            if (!SD.rename(kTempPath, finalizing.path)) {
                addError();
                ok = false;
            }
        }
        setState(ok ? MASTER_REC_COMPLETE : MASTER_REC_ERROR);
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
    s_snapshot.state = sdMounted ? MASTER_REC_IDLE : MASTER_REC_UNAVAILABLE;
    s_snapshot.path[0] = 0;
    s_snapshot.framesWritten = 0;
    s_snapshot.droppedFrames = 0;
    s_snapshot.ringHighWater = 0;
    s_snapshot.maxWriteUs = 0;
    s_snapshot.errors = sdMounted ? 0 : 1;
    s_task = nullptr;
    portEXIT_CRITICAL(&s_mux);
}

bool masterRecorderStart() {
    if (!s_sdMounted || masterRecorderIsBusy() || sdDiagnosticsIsRunning() || micRecActive())
        return false;
    if (!selectOutputPath()) return false;

    s_ring.reset();
    portENTER_CRITICAL(&s_mux);
    s_snapshot.state = MASTER_REC_STARTING;
    s_snapshot.framesWritten = 0;
    s_snapshot.droppedFrames = 0;
    s_snapshot.ringHighWater = 0;
    s_snapshot.maxWriteUs = 0;
    s_snapshot.errors = 0;
    portEXIT_CRITICAL(&s_mux);

    if (xTaskCreatePinnedToCore(recorderTask, "master_wav", 6144, nullptr, 1,
                                &s_task, 1) == pdPASS)
        return true;

    addError();
    setState(MASTER_REC_ERROR);
    return false;
}

bool masterRecorderStop() {
    const MasterRecorderState state = getState();
    if (state != MASTER_REC_STARTING && state != MASTER_REC_RECORDING) return false;
    setState(MASTER_REC_STOPPING);
    return true;
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
    if (pushed < count)
        s_snapshot.droppedFrames += static_cast<uint32_t>(count - pushed);
    if (highWater > s_snapshot.ringHighWater) s_snapshot.ringHighWater = highWater;
    portEXIT_CRITICAL(&s_mux);
}

MasterRecorderSnapshot masterRecorderSnapshot() {
    portENTER_CRITICAL(&s_mux);
    const MasterRecorderSnapshot copy = s_snapshot;
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
