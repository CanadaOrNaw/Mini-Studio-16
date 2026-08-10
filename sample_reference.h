#pragma once

#include "sampler_slots.h"

#include <stdint.h>

// RAM sample registry indexes are non-negative. Direct streamed slots use
// -2..-17 so -1 remains the inherited "no sample" sentinel.
inline int8_t sampleEncodeStreamReference(uint8_t streamSlot) {
    return streamSlot < SAMPLER_SLOT_COUNT
        ? static_cast<int8_t>(-2 - static_cast<int8_t>(streamSlot)) : -1;
}

inline bool sampleDecodeStreamReference(int reference, uint8_t& streamSlot) {
    if (reference > -2 || reference < -1 - SAMPLER_SLOT_COUNT) return false;
    streamSlot = static_cast<uint8_t>(-2 - reference);
    return streamSlot < SAMPLER_SLOT_COUNT;
}
