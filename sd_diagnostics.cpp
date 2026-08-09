#include "sd_diagnostics.h"

#include "config.h"
#include "master_recorder.h"
#include <Arduino.h>
#include <SD.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
constexpr size_t kChunkBytes = 4096;
constexpr size_t kFileBytes = 512 * 1024;
constexpr uint8_t kFiles = 6;
constexpr uint32_t kMinWriteKBs = 500;
constexpr uint32_t kMinReadKBs = 1000;
constexpr uint32_t kMinRoundRobinKBs = 1000;
constexpr uint32_t kMaxStallUs = 75000;
constexpr const char* kDiagDir = DIR_ROOT "/diag";

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
SdDiagSnapshot s_snapshot = {SD_DIAG_IDLE, "READY", 0, 0, 0, 0, 0, 0, 0};
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;

void resetSnapshot(SdDiagState state, const char* step, uint32_t errors) {
    s_snapshot.state = state;
    strncpy(s_snapshot.step, step, sizeof(s_snapshot.step) - 1);
    s_snapshot.step[sizeof(s_snapshot.step) - 1] = 0;
    s_snapshot.writeKBs = 0;
    s_snapshot.readKBs = 0;
    s_snapshot.roundRobinKBs = 0;
    s_snapshot.maxWriteUs = 0;
    s_snapshot.maxReadUs = 0;
    s_snapshot.minFreeHeap = 0;
    s_snapshot.errors = errors;
}

void setStep(const char* step) {
    portENTER_CRITICAL(&s_mux);
    strncpy(s_snapshot.step, step, sizeof(s_snapshot.step) - 1);
    s_snapshot.step[sizeof(s_snapshot.step) - 1] = 0;
    portEXIT_CRITICAL(&s_mux);
}

void addError() {
    portENTER_CRITICAL(&s_mux);
    ++s_snapshot.errors;
    portEXIT_CRITICAL(&s_mux);
}

void noteHeap() {
    const uint32_t freeHeap = heap_caps_get_free_size(MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL);
    portENTER_CRITICAL(&s_mux);
    if (s_snapshot.minFreeHeap == 0 || freeHeap < s_snapshot.minFreeHeap)
        s_snapshot.minFreeHeap = freeHeap;
    portEXIT_CRITICAL(&s_mux);
}

void noteLatency(bool write, uint32_t elapsedUs) {
    portENTER_CRITICAL(&s_mux);
    uint32_t& maximum = write ? s_snapshot.maxWriteUs : s_snapshot.maxReadUs;
    if (elapsedUs > maximum) maximum = elapsedUs;
    portEXIT_CRITICAL(&s_mux);
}

uint32_t throughputKBs(uint64_t bytes, uint32_t elapsedUs) {
    if (elapsedUs == 0) return 0;
    return static_cast<uint32_t>((bytes * 1000000ULL) / elapsedUs / 1024ULL);
}

void filePath(uint8_t index, char* path, size_t length) {
    snprintf(path, length, "%s/stream%u.bin", kDiagDir, static_cast<unsigned>(index + 1));
}

bool writeFiles(const uint8_t* block) {
    setStep("WRITE 6 FILES");
    uint64_t totalBytes = 0;
    const uint32_t started = micros();

    for (uint8_t index = 0; index < kFiles; ++index) {
        char path[64];
        filePath(index, path, sizeof(path));
        SD.remove(path);
        const uint32_t openStart = micros();
        File file = SD.open(path, FILE_WRITE);
        noteLatency(true, micros() - openStart);
        if (!file) { addError(); return false; }

        for (size_t offset = 0; offset < kFileBytes; offset += kChunkBytes) {
            const uint32_t opStart = micros();
            const size_t written = file.write(block, kChunkBytes);
            noteLatency(true, micros() - opStart);
            if (written != kChunkBytes) {
                addError();
                file.close();
                return false;
            }
            totalBytes += written;
            noteHeap();
            taskYIELD();
        }

        const uint32_t flushStart = micros();
        file.flush();
        noteLatency(true, micros() - flushStart);
        const uint32_t closeStart = micros();
        file.close();
        noteLatency(true, micros() - closeStart);
    }

    const uint32_t rate = throughputKBs(totalBytes, micros() - started);
    portENTER_CRITICAL(&s_mux);
    s_snapshot.writeKBs = rate;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool sequentialRead(const uint8_t* expected, uint8_t* block) {
    setStep("SEQ READ");
    char path[64];
    filePath(0, path, sizeof(path));
    const uint32_t openStart = micros();
    File file = SD.open(path, FILE_READ);
    noteLatency(false, micros() - openStart);
    if (!file) { addError(); return false; }

    uint64_t totalBytes = 0;
    const uint32_t started = micros();
    while (totalBytes < kFileBytes) {
        const uint32_t opStart = micros();
        const int got = file.read(block, kChunkBytes);
        noteLatency(false, micros() - opStart);
        if (got != static_cast<int>(kChunkBytes) || memcmp(block, expected, kChunkBytes) != 0) {
            addError();
            file.close();
            return false;
        }
        totalBytes += static_cast<uint32_t>(got);
        noteHeap();
        taskYIELD();
    }
    const uint32_t closeStart = micros();
    file.close();
    noteLatency(false, micros() - closeStart);

    const uint32_t rate = throughputKBs(totalBytes, micros() - started);
    portENTER_CRITICAL(&s_mux);
    s_snapshot.readKBs = rate;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

bool roundRobinRead(const uint8_t* expected, uint8_t* block) {
    setStep("6-FILE READ");
    File files[kFiles];
    size_t remaining[kFiles];
    for (uint8_t index = 0; index < kFiles; ++index) {
        char path[64];
        filePath(index, path, sizeof(path));
        const uint32_t openStart = micros();
        files[index] = SD.open(path, FILE_READ);
        noteLatency(false, micros() - openStart);
        remaining[index] = kFileBytes;
        if (!files[index]) {
            addError();
            for (uint8_t closeIndex = 0; closeIndex <= index; ++closeIndex) files[closeIndex].close();
            return false;
        }
    }

    uint64_t totalBytes = 0;
    const uint32_t started = micros();
    bool workLeft = true;
    while (workLeft) {
        workLeft = false;
        for (uint8_t index = 0; index < kFiles; ++index) {
            if (remaining[index] == 0) continue;
            workLeft = true;
            const size_t requested = remaining[index] < kChunkBytes ? remaining[index] : kChunkBytes;
            const uint32_t opStart = micros();
            const int got = files[index].read(block, requested);
            noteLatency(false, micros() - opStart);
            if (got != static_cast<int>(requested) || memcmp(block, expected, requested) != 0) {
                addError();
                for (auto& file : files) file.close();
                return false;
            }
            remaining[index] -= static_cast<size_t>(got);
            totalBytes += static_cast<uint32_t>(got);
            noteHeap();
        }
        taskYIELD();
    }
    for (auto& file : files) file.close();

    const uint32_t rate = throughputKBs(totalBytes, micros() - started);
    portENTER_CRITICAL(&s_mux);
    s_snapshot.roundRobinKBs = rate;
    portEXIT_CRITICAL(&s_mux);
    return true;
}

void cleanup() {
    for (uint8_t index = 0; index < kFiles; ++index) {
        char path[64];
        filePath(index, path, sizeof(path));
        SD.remove(path);
    }
    SD.rmdir(kDiagDir);
}

void diagTask(void*) {
    uint8_t* writeBlock = static_cast<uint8_t*>(heap_caps_malloc(
        kChunkBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    uint8_t* readBlock = static_cast<uint8_t*>(heap_caps_malloc(
        kChunkBytes, MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

    bool ok = writeBlock && readBlock;
    if (!ok) addError();
    if (ok) {
        for (size_t i = 0; i < kChunkBytes; ++i)
            writeBlock[i] = static_cast<uint8_t>((i * 37u + 11u) & 0xFFu);
        if (!SD.exists(DIR_ROOT)) SD.mkdir(DIR_ROOT);
        if (!SD.exists(kDiagDir) && !SD.mkdir(kDiagDir)) { addError(); ok = false; }
    }

    if (ok) ok = writeFiles(writeBlock);
    if (ok) ok = sequentialRead(writeBlock, readBlock);
    if (ok) ok = roundRobinRead(writeBlock, readBlock);
    setStep("CLEANUP");
    cleanup();

    if (writeBlock) heap_caps_free(writeBlock);
    if (readBlock) heap_caps_free(readBlock);

    portENTER_CRITICAL(&s_mux);
    const bool thresholds =
        s_snapshot.writeKBs >= kMinWriteKBs &&
        s_snapshot.readKBs >= kMinReadKBs &&
        s_snapshot.roundRobinKBs >= kMinRoundRobinKBs &&
        s_snapshot.maxWriteUs <= kMaxStallUs &&
        s_snapshot.maxReadUs <= kMaxStallUs &&
        s_snapshot.errors == 0;
    s_snapshot.state = (ok && thresholds) ? SD_DIAG_PASS : SD_DIAG_FAIL;
    strncpy(s_snapshot.step, s_snapshot.state == SD_DIAG_PASS ? "PASS" : "FAIL",
            sizeof(s_snapshot.step) - 1);
    s_snapshot.step[sizeof(s_snapshot.step) - 1] = 0;
    const SdDiagSnapshot finalSnapshot = s_snapshot;
    portEXIT_CRITICAL(&s_mux);

    Serial.printf(
        "SDDIAG state=%s write=%luKB/s read=%luKB/s rr6=%luKB/s "
        "maxWrite=%luus maxRead=%luus minHeap=%lu errors=%lu\n",
        finalSnapshot.state == SD_DIAG_PASS ? "PASS" : "FAIL",
        static_cast<unsigned long>(finalSnapshot.writeKBs),
        static_cast<unsigned long>(finalSnapshot.readKBs),
        static_cast<unsigned long>(finalSnapshot.roundRobinKBs),
        static_cast<unsigned long>(finalSnapshot.maxWriteUs),
        static_cast<unsigned long>(finalSnapshot.maxReadUs),
        static_cast<unsigned long>(finalSnapshot.minFreeHeap),
        static_cast<unsigned long>(finalSnapshot.errors));

    portENTER_CRITICAL(&s_mux);
    s_task = nullptr;
    portEXIT_CRITICAL(&s_mux);
    vTaskDelete(nullptr);
}
}  // namespace

void sdDiagnosticsInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    portENTER_CRITICAL(&s_mux);
    resetSnapshot(sdMounted ? SD_DIAG_IDLE : SD_DIAG_FAIL,
                  sdMounted ? "READY" : "NO SD", sdMounted ? 0u : 1u);
    portEXIT_CRITICAL(&s_mux);
}

bool sdDiagnosticsStart() {
    if (!s_sdMounted || sdDiagnosticsIsRunning() || masterRecorderIsBusy()) return false;
    portENTER_CRITICAL(&s_mux);
    resetSnapshot(SD_DIAG_RUNNING, "STARTING", 0);
    portEXIT_CRITICAL(&s_mux);
    if (xTaskCreatePinnedToCore(diagTask, "sd_diag", 6144, nullptr, 1, &s_task, 1) == pdPASS)
        return true;
    portENTER_CRITICAL(&s_mux);
    s_snapshot.state = SD_DIAG_FAIL;
    strncpy(s_snapshot.step, "TASK FAILED", sizeof(s_snapshot.step) - 1);
    s_snapshot.step[sizeof(s_snapshot.step) - 1] = 0;
    ++s_snapshot.errors;
    portEXIT_CRITICAL(&s_mux);
    return false;
}

bool sdDiagnosticsIsRunning() {
    portENTER_CRITICAL(&s_mux);
    const bool running = s_snapshot.state == SD_DIAG_RUNNING;
    portEXIT_CRITICAL(&s_mux);
    return running;
}

SdDiagSnapshot sdDiagnosticsSnapshot() {
    portENTER_CRITICAL(&s_mux);
    const SdDiagSnapshot copy = s_snapshot;
    portEXIT_CRITICAL(&s_mux);
    return copy;
}
