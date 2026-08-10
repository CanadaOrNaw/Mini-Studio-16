#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioCapSnapshot {
    bool initialized;
    bool detected;
    bool lineMonitor;
    bool bluetoothConnected;
    uint8_t monitorPercent;
    uint16_t status;
    uint32_t transfers;
    uint32_t crcErrors;
    uint32_t sequenceGaps;
    uint32_t playbackDrops;
    uint32_t captureUnderruns;
    uint32_t capUnderruns;
    uint32_t capOverruns;
};

void audioCapInit();
void audioCapProcessAudioBlock(int16_t* master, size_t frames);
AudioCapSnapshot audioCapSnapshot();
void audioCapSetMonitor(uint8_t percent);
void audioCapRequestPair();
void audioCapRequestDisconnect();
void audioCapRequestClearStats();
