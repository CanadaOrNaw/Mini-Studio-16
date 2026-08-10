#include "motion.h"

#include "event_looper.h"
#include "sequencer.h"
#include "midi_output.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <string.h>

namespace {
MotionFilter s_filter;
MotionSnapshot s_snapshot = {};
uint32_t s_lastUpdateMs = 0;
uint16_t s_lastRecordStep[MOTION_MAPPING_COUNT];
uint8_t s_lastRecordValue[MOTION_MAPPING_COUNT];
uint8_t s_lastOutputValue[MOTION_MAPPING_COUNT];

void applyTarget(uint8_t target, uint8_t value) {
    if (target == MOTION_TARGET_NONE || target >= MOTION_TARGET_COUNT) return;
    const float normalized = static_cast<float>(value) / 127.0f;
    uint8_t synth = 0;
    bool resonance = false;
    switch (target) {
        case MOTION_TARGET_SYNTH1_CUTOFF: synth = 0; break;
        case MOTION_TARGET_SYNTH2_CUTOFF: synth = 1; break;
        case MOTION_TARGET_SYNTH3_CUTOFF: synth = 2; break;
        case MOTION_TARGET_SYNTH1_RESONANCE: synth = 0; resonance = true; break;
        case MOTION_TARGET_SYNTH2_RESONANCE: synth = 1; resonance = true; break;
        case MOTION_TARGET_SYNTH3_RESONANCE: synth = 2; resonance = true; break;
        default: return;
    }
    g_synths[synth].forEach([&](SynthVoice& voice) {
        if (resonance) voice.fltReso = normalized;
        else voice.fltCutoff = 0.02f + normalized * 0.96f;
    });
}

void applyMappings() {
    const uint16_t recordStep = sequencerEventRecordStep();
    for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping) {
        const MotionMapping& item = s_snapshot.mappings[mapping];
        if (item.source == MOTION_SOURCE_NONE || item.source >= MOTION_SOURCE_COUNT ||
            item.target == MOTION_TARGET_NONE || item.target >= MOTION_TARGET_COUNT)
            continue;
        const uint8_t value = s_snapshot.values[item.source];
        applyTarget(item.target, value);
        const uint8_t outputDelta = value > s_lastOutputValue[mapping]
            ? value - s_lastOutputValue[mapping] : s_lastOutputValue[mapping] - value;
        if (outputDelta >= 2) {
            midiOutputControlChange(15, static_cast<uint8_t>(16 + mapping), value);
            s_lastOutputValue[mapping] = value;
        }
        const uint8_t delta = value > s_lastRecordValue[mapping]
            ? value - s_lastRecordValue[mapping] : s_lastRecordValue[mapping] - value;
        if (recordStep != s_lastRecordStep[mapping] && delta >= 2 &&
            eventLooperRecordControl(recordStep, item.target, value)) {
            s_lastRecordStep[mapping] = recordStep;
            s_lastRecordValue[mapping] = value;
        }
    }
}
}  // namespace

void motionInit() {
    s_filter.reset();
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.available = M5.Imu.isEnabled();
    motionResetMappings();
    for (uint8_t index = 0; index < MOTION_MAPPING_COUNT; ++index) {
        s_lastRecordStep[index] = 0xFFFF;
        s_lastRecordValue[index] = 0;
        s_lastOutputValue[index] = 0;
    }
    s_lastUpdateMs = 0;
}

void motionResetMappings() {
    for (uint8_t index = 0; index < MOTION_MAPPING_COUNT; ++index)
        s_snapshot.mappings[index] = MotionMapping{MOTION_SOURCE_NONE,
                                                   MOTION_TARGET_NONE};
    s_snapshot.mappings[0] = MotionMapping{MOTION_SOURCE_TILT_X,
                                           MOTION_TARGET_SYNTH1_CUTOFF};
    s_snapshot.mappings[1] = MotionMapping{MOTION_SOURCE_TILT_Y,
                                           MOTION_TARGET_SYNTH2_CUTOFF};
}

void motionUpdate() {
    if (!s_snapshot.available) return;
    const uint32_t now = millis();
    if (now - s_lastUpdateMs < 10) return;
    s_lastUpdateMs = now;
    if (!M5.Imu.update()) return;
    const m5::imu_data_t data = M5.Imu.getImuData();
    MotionInput input = {data.accel.x, data.accel.y, data.accel.z,
                         data.gyro.x, data.gyro.y, data.gyro.z, now};
    const MotionOutput output = s_filter.update(input);
    memcpy(s_snapshot.values, output.values, sizeof(output.values));
    s_snapshot.gestures = output.gestures;
    ++s_snapshot.samples;
    applyMappings();
}

bool motionSetMapping(uint8_t mapping, MotionSource source, MotionTarget target) {
    if (mapping >= MOTION_MAPPING_COUNT || source <= MOTION_SOURCE_NONE ||
        source >= MOTION_SOURCE_COUNT || target <= MOTION_TARGET_NONE ||
        target >= MOTION_TARGET_COUNT) return false;
    s_snapshot.mappings[mapping] = MotionMapping{source, target};
    s_lastRecordStep[mapping] = 0xFFFF;
    return true;
}

void motionClearMapping(uint8_t mapping) {
    if (mapping < MOTION_MAPPING_COUNT)
        s_snapshot.mappings[mapping] = MotionMapping{MOTION_SOURCE_NONE,
                                                      MOTION_TARGET_NONE};
}

void motionApplyRecordedControl(uint8_t target, uint8_t value) {
    applyTarget(target, value);
}

MotionSnapshot motionSnapshot() { return s_snapshot; }

const char* motionSourceName(uint8_t source) {
    switch (source) {
        case MOTION_SOURCE_NONE: return "none";
        case MOTION_SOURCE_TILT_X: return "tilt_x";
        case MOTION_SOURCE_TILT_Y: return "tilt_y";
        case MOTION_SOURCE_ACCEL: return "accel";
        case MOTION_SOURCE_GYRO: return "gyro";
        case MOTION_SOURCE_SHAKE: return "shake";
        case MOTION_SOURCE_SLAP: return "slap";
        default: return "unknown";
    }
}

const char* motionTargetName(uint8_t target) {
    switch (target) {
        case MOTION_TARGET_NONE: return "none";
        case MOTION_TARGET_SYNTH1_CUTOFF: return "synth1_cutoff";
        case MOTION_TARGET_SYNTH2_CUTOFF: return "synth2_cutoff";
        case MOTION_TARGET_SYNTH3_CUTOFF: return "synth3_cutoff";
        case MOTION_TARGET_SYNTH1_RESONANCE: return "synth1_resonance";
        case MOTION_TARGET_SYNTH2_RESONANCE: return "synth2_resonance";
        case MOTION_TARGET_SYNTH3_RESONANCE: return "synth3_resonance";
        default: return "unknown";
    }
}
