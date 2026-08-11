#pragma once

#include <stdint.h>

enum SdDiagState : uint8_t {
    SD_DIAG_IDLE = 0,
    SD_DIAG_RUNNING,
    SD_DIAG_PASS,
    SD_DIAG_FAIL,
};

struct SdDiagSnapshot {
    SdDiagState state;
    char step[16];
    uint32_t writeKBs;
    uint32_t readKBs;
    uint32_t roundRobinKBs;
    uint32_t maxWriteUs;
    uint32_t maxReadUs;
    uint32_t minFreeHeap;
    uint32_t errors;
};

void sdDiagnosticsInit(bool sdMounted);
bool sdDiagnosticsStart();
bool sdDiagnosticsIsRunning();
SdDiagSnapshot sdDiagnosticsSnapshot();

