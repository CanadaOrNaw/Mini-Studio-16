#pragma once

#include <Arduino.h>

struct SdIoSnapshot {
    bool available;
    uint32_t acquisitions;
    uint32_t contentions;
    uint32_t maxWaitUs;
    uint32_t maxHoldUs;
};

void sdIoInit();
SdIoSnapshot sdIoSnapshot();

// All FatFS/SDSPI calls from worker tasks pass through this recursive mutex.
// Audio rendering never takes it; it only exchanges PCM through lock-free rings.
class SdIoGuard {
public:
    SdIoGuard();
    ~SdIoGuard();
    explicit operator bool() const { return _locked; }

    SdIoGuard(const SdIoGuard&) = delete;
    SdIoGuard& operator=(const SdIoGuard&) = delete;

private:
    bool _locked;
    uint32_t _lockedAtUs;
};
