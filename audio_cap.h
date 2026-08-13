#pragma once

#include <stddef.h>
#include <stdint.h>

struct AudioCapSnapshot {
    bool present;
    bool a2dpConnected;
    bool discovering;
    bool adcLocked;
    bool fault;
    uint8_t monitorPercent;
    uint32_t transfers;
    uint32_t transferErrors;
    uint32_t playbackDrops;
    uint32_t captureDrops;
    uint32_t playbackUnderruns;
    uint32_t captureUnderruns;
    uint32_t sequenceGaps;
};

void audioCapInit();
void audioCapProcessAudioBlock(int16_t* masterPcm, size_t frames);
// Most recent line-input frame for vocoder modulation. Returns false when no
// cap is attached (or it has produced no audio yet), which tells the caller
// to leave the dry bus alone rather than vocode against silence.
bool audioCapLineModulator(size_t frame, int16_t& sample);
AudioCapSnapshot audioCapSnapshot();
void audioCapRequestPair();
void audioCapRequestDisconnect();
void audioCapClearStats();
void audioCapSetMonitor(uint8_t percent);
