#pragma once

#include <stdint.h>

static const uint32_t WAV_PCM_HEADER_BYTES = 44;

struct WavRecoveryPlan {
    bool recoverable;
    uint32_t frames;
    uint32_t dataBytes;
    uint8_t ignoredTrailingBytes;
};

struct WavMono16Info {
    uint32_t sampleRate;
    uint32_t frames;
};

void wavBuildMono16Header(uint8_t header[WAV_PCM_HEADER_BYTES],
                          uint32_t sampleRate, uint32_t frames);
WavRecoveryPlan wavPlanMono16Recovery(uint32_t fileBytes);
bool wavParseCanonicalMono16Header(const uint8_t header[WAV_PCM_HEADER_BYTES],
                                   WavMono16Info& info);
