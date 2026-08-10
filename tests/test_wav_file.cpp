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

    WavMono16Info info = {};
    assert(wavParseCanonicalMono16Header(header, info));
    assert(info.sampleRate == 22050 && info.frames == 44100);
    uint8_t invalid[WAV_PCM_HEADER_BYTES];
    std::memcpy(invalid, header, sizeof(invalid));
    invalid[22] = 2;
    assert(!wavParseCanonicalMono16Header(invalid, info));

    WavRecoveryPlan recovery = wavPlanMono16Recovery(44 + 88201);
    assert(recovery.recoverable);
    assert(recovery.frames == 44100);
    assert(recovery.dataBytes == 88200);
    assert(recovery.ignoredTrailingBytes == 1);
    assert(!wavPlanMono16Recovery(45).recoverable);
    std::cout << "wav_file: all tests passed\n";
    return 0;
}
