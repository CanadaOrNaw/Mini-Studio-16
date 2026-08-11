#pragma once

#include "motion_core.h"

#include <stdint.h>

static const uint8_t MOTION_MAPPING_COUNT = 4;

struct MotionSnapshot {
    bool available;
    uint8_t values[MOTION_SOURCE_COUNT];
    uint8_t gestures;
    MotionMapping mappings[MOTION_MAPPING_COUNT];
    uint32_t samples;
};

void motionInit();
void motionResetMappings();
void motionUpdate();
bool motionSetMapping(uint8_t mapping, MotionSource source, MotionTarget target);
void motionClearMapping(uint8_t mapping);
void motionApplyRecordedControl(uint8_t target, uint8_t value);
MotionSnapshot motionSnapshot();
const char* motionSourceName(uint8_t source);
const char* motionTargetName(uint8_t target);
