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

