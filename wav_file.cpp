#include "wav_file.h"

#include <string.h>

namespace {
void putU16(uint8_t* destination, uint16_t value) {
    destination[0] = static_cast<uint8_t>(value & 0xFFu);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
}

void putU32(uint8_t* destination, uint32_t value) {
    destination[0] = static_cast<uint8_t>(value & 0xFFu);
    destination[1] = static_cast<uint8_t>((value >> 8) & 0xFFu);
    destination[2] = static_cast<uint8_t>((value >> 16) & 0xFFu);
    destination[3] = static_cast<uint8_t>((value >> 24) & 0xFFu);
}

uint16_t getU16(const uint8_t* source) {
    return static_cast<uint16_t>(source[0]) |
           static_cast<uint16_t>(source[1] << 8);
}

uint32_t getU32(const uint8_t* source) {
    return static_cast<uint32_t>(source[0]) |
           (static_cast<uint32_t>(source[1]) << 8) |
           (static_cast<uint32_t>(source[2]) << 16) |
           (static_cast<uint32_t>(source[3]) << 24);
}
}  // namespace

void wavBuildMono16Header(uint8_t header[WAV_PCM_HEADER_BYTES],
                          uint32_t sampleRate, uint32_t frames) {
    memset(header, 0, WAV_PCM_HEADER_BYTES);
    memcpy(header + 0, "RIFF", 4);
    putU32(header + 4, 36u + frames * 2u);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    putU32(header + 16, 16);
    putU16(header + 20, 1);
    putU16(header + 22, 1);
    putU32(header + 24, sampleRate);
    putU32(header + 28, sampleRate * 2u);
    putU16(header + 32, 2);
    putU16(header + 34, 16);
    memcpy(header + 36, "data", 4);
    putU32(header + 40, frames * 2u);
}

WavRecoveryPlan wavPlanMono16Recovery(uint32_t fileBytes) {
    WavRecoveryPlan plan = {false, 0, 0, 0};
    if (fileBytes <= WAV_PCM_HEADER_BYTES + 1u) return plan;
    const uint32_t payload = fileBytes - WAV_PCM_HEADER_BYTES;
    plan.frames = payload / 2u;
    plan.dataBytes = plan.frames * 2u;
    plan.ignoredTrailingBytes = static_cast<uint8_t>(payload & 1u);
    plan.recoverable = plan.frames > 0;
    return plan;
}

bool wavParseCanonicalMono16Header(const uint8_t header[WAV_PCM_HEADER_BYTES],
                                   WavMono16Info& info) {
    info.sampleRate = 0;
    info.frames = 0;
    if (!header || memcmp(header, "RIFF", 4) != 0 ||
        memcmp(header + 8, "WAVEfmt ", 8) != 0 ||
        getU32(header + 16) != 16 || getU16(header + 20) != 1 ||
        getU16(header + 22) != 1 || getU16(header + 34) != 16 ||
        memcmp(header + 36, "data", 4) != 0)
        return false;
    const uint32_t sampleRate = getU32(header + 24);
    const uint32_t dataBytes = getU32(header + 40);
    if (sampleRate == 0 || getU32(header + 28) != sampleRate * 2u ||
        getU16(header + 32) != 2 || (dataBytes & 1u) != 0)
        return false;
    info.sampleRate = sampleRate;
    info.frames = dataBytes / 2u;
    return true;
}
