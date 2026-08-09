#include "../wav_file.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

static uint32_t readU32(const uint8_t* source) {
    return static_cast<uint32_t>(source[0]) |
           (static_cast<uint32_t>(source[1]) << 8) |
           (static_cast<uint32_t>(source[2]) << 16) |
           (static_cast<uint32_t>(source[3]) << 24);
}

int main() {
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, 22050, 44100);
    assert(std::memcmp(header, "RIFF", 4) == 0);
    assert(std::memcmp(header + 8, "WAVEfmt ", 8) == 0);
    assert(readU32(header + 4) == 36 + 88200);
    assert(readU32(header + 24) == 22050);
    assert(readU32(header + 28) == 44100);
    assert(readU32(header + 40) == 88200);
    std::cout << "wav_file: all tests passed\n";
    return 0;
}
