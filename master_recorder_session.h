#pragma once

#include "master_recorder.h"

#include <string.h>

// Pure state/metrics model shared by firmware and host tests. Thread safety is
// provided by the firmware wrapper because producer and storage tasks touch it.
class MasterRecorderSession {
public:
    MasterRecorderSession() { initialize(false); }

    void initialize(bool available) {
        memset(&_snapshot, 0, sizeof(_snapshot));
        _snapshot.state = available ? MASTER_REC_IDLE : MASTER_REC_UNAVAILABLE;
        _snapshot.errors = available ? 0u : 1u;
    }

    bool begin(const char* path) {
        if (_snapshot.state == MASTER_REC_STARTING ||
            _snapshot.state == MASTER_REC_RECORDING ||
            _snapshot.state == MASTER_REC_STOPPING)
            return false;
        const uint32_t recoveredFrames = _snapshot.recoveredFrames;
        char recoveredPath[sizeof(_snapshot.recoveredPath)];
        memcpy(recoveredPath, _snapshot.recoveredPath, sizeof(recoveredPath));
        memset(&_snapshot, 0, sizeof(_snapshot));
        _snapshot.state = MASTER_REC_STARTING;
        _snapshot.recoveredFrames = recoveredFrames;
        memcpy(_snapshot.recoveredPath, recoveredPath, sizeof(_snapshot.recoveredPath));
        _snapshot.recoveredPath[sizeof(_snapshot.recoveredPath) - 1] = 0;
        copyText(_snapshot.path, sizeof(_snapshot.path), path);
        return true;
    }

    bool markRecording() {
        if (_snapshot.state != MASTER_REC_STARTING) return false;
        _snapshot.state = MASTER_REC_RECORDING;
        return true;
    }

    bool requestStop() {
        if (_snapshot.state != MASTER_REC_STARTING &&
            _snapshot.state != MASTER_REC_RECORDING)
            return false;
        _snapshot.state = MASTER_REC_STOPPING;
        return true;
    }

    void noteProduced(uint32_t requested, uint32_t accepted, uint32_t highWater) {
        if (accepted < requested) _snapshot.droppedFrames += requested - accepted;
        if (highWater > _snapshot.ringHighWater) _snapshot.ringHighWater = highWater;
    }

    void noteWrite(uint32_t frames, uint32_t elapsedUs) {
        _snapshot.framesWritten += frames;
        if (elapsedUs > _snapshot.maxWriteUs) _snapshot.maxWriteUs = elapsedUs;
    }

    void noteError() { ++_snapshot.errors; }

    void noteRecovery(const char* path, uint32_t frames) {
        _snapshot.recoveredFrames = frames;
        copyText(_snapshot.recoveredPath, sizeof(_snapshot.recoveredPath), path);
    }

    void complete() { _snapshot.state = MASTER_REC_COMPLETE; }
    void fail() { _snapshot.state = MASTER_REC_ERROR; }
    MasterRecorderState state() const { return _snapshot.state; }
    MasterRecorderSnapshot snapshot() const { return _snapshot; }

private:
    MasterRecorderSnapshot _snapshot;

    static void copyText(char* destination, size_t capacity, const char* source) {
        if (!destination || capacity == 0) return;
        if (!source) { destination[0] = 0; return; }
        size_t length = strlen(source);
        if (length >= capacity) length = capacity - 1;
        memcpy(destination, source, length);
        destination[length] = 0;
    }
};
