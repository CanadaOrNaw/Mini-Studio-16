#include "stem_file.h"

#include <string.h>

namespace {
void putU16(uint8_t* output, uint16_t value) {
    output[0] = static_cast<uint8_t>(value);
    output[1] = static_cast<uint8_t>(value >> 8);
}
void putU32(uint8_t* output, uint32_t value) {
    for (uint8_t index = 0; index < 4; ++index)
        output[index] = static_cast<uint8_t>(value >> (index * 8));
}
uint16_t getU16(const uint8_t* input) {
    return static_cast<uint16_t>(input[0] | (input[1] << 8));
}
uint32_t getU32(const uint8_t* input) {
    return static_cast<uint32_t>(input[0]) |
           (static_cast<uint32_t>(input[1]) << 8) |
           (static_cast<uint32_t>(input[2]) << 16) |
           (static_cast<uint32_t>(input[3]) << 24);
}
}  // namespace

void stemBuildHeader(uint8_t header[STEM_FILE_HEADER_BYTES], uint32_t sampleRate,
                     uint32_t frames) {
    memset(header, 0, STEM_FILE_HEADER_BYTES);
    memcpy(header, "MS16STEM", 8);
    putU16(header + 8, STEM_FILE_VERSION);
    putU16(header + 10, STEM_FILE_CHANNELS);
    putU32(header + 12, sampleRate);
    putU32(header + 16, frames);
    memcpy(header + 20, "M123D", 5);  // master, synth 1/2/3, drums
}

bool stemParseHeader(const uint8_t header[STEM_FILE_HEADER_BYTES], StemFileInfo& info) {
    memset(&info, 0, sizeof(info));
    if (memcmp(header, "MS16STEM", 8) != 0) return false;
    info.version = getU16(header + 8);
    info.channels = getU16(header + 10);
    info.sampleRate = getU32(header + 12);
    info.frames = getU32(header + 16);
    return info.version == STEM_FILE_VERSION && info.channels == STEM_FILE_CHANNELS &&
           info.sampleRate > 0 && memcmp(header + 20, "M123D", 5) == 0;
}

void stemInterleaveFrames(const StemPcmFrame* frames, uint32_t count, int16_t* output) {
    if (!frames || !output) return;
    for (uint32_t index = 0; index < count; ++index) {
        output[index * 5 + 0] = frames[index].master;
        output[index * 5 + 1] = frames[index].synth1;
        output[index * 5 + 2] = frames[index].synth2;
        output[index * 5 + 3] = frames[index].synth3;
        output[index * 5 + 4] = frames[index].drums;
    }
}
