#pragma once
#include <stdint.h>

static inline uint32_t swingStepPeriod(uint32_t straightPeriod, uint8_t swing,
                                       uint8_t step) {
    if (swing < 50) swing = 50; else if (swing > 75) swing = 75;
    const uint32_t factor = (step & 1u) ? 100u - swing : swing;
    return straightPeriod * factor / 50u;
}
