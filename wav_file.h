#pragma once

#include <stdint.h>

static const uint32_t WAV_PCM_HEADER_BYTES = 44;
// P3 (reconciliation report): keep every produced WAV below 2 GiB total so
// the u32 RIFF/data size fields can never wrap and FAT32/parser limits are
// never approached. The recorders stop long before this; the builder also
// clamps defensively (recovery of an oversized temp truncates, not wraps).
static const uint32_t WAV_MONO16_MAX_FRAMES =
    (0x7FFFFFFFu - WAV_PCM_HEADER_BYTES) / 2u;

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
