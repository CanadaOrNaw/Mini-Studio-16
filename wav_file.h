#pragma once

#include <stdint.h>

static const uint32_t WAV_PCM_HEADER_BYTES = 44;

void wavBuildMono16Header(uint8_t header[WAV_PCM_HEADER_BYTES],
                          uint32_t sampleRate, uint32_t frames);

