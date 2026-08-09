#pragma once

#include <stdint.h>

static const uint32_t STEM_FILE_HEADER_BYTES = 32;
static const uint16_t STEM_FILE_VERSION = 1;
static const uint16_t STEM_FILE_CHANNELS = 5;

struct StemFileInfo {
    uint16_t version;
    uint16_t channels;
    uint32_t sampleRate;
    uint32_t frames;
};

struct StemPcmFrame {
    int16_t master;
    int16_t synth1;
    int16_t synth2;
    int16_t synth3;
    int16_t drums;
};
static_assert(sizeof(StemPcmFrame) == 10, "stem PCM frame layout changed");

void stemBuildHeader(uint8_t header[STEM_FILE_HEADER_BYTES], uint32_t sampleRate,
                     uint32_t frames);
bool stemParseHeader(const uint8_t header[STEM_FILE_HEADER_BYTES], StemFileInfo& info);
void stemInterleaveFrames(const StemPcmFrame* frames, uint32_t count, int16_t* output);
