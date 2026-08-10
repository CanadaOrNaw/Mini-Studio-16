#pragma once

#include <math.h>
#include <stdint.h>

enum MotionSource : uint8_t {
    MOTION_SOURCE_NONE = 0,
    MOTION_SOURCE_TILT_X,
    MOTION_SOURCE_TILT_Y,
    MOTION_SOURCE_ACCEL,
    MOTION_SOURCE_GYRO,
    MOTION_SOURCE_SHAKE,
    MOTION_SOURCE_SLAP,
    MOTION_SOURCE_COUNT,
};

enum MotionTarget : uint8_t {
    MOTION_TARGET_NONE = 0,
    MOTION_TARGET_SYNTH1_CUTOFF,
    MOTION_TARGET_SYNTH2_CUTOFF,
    MOTION_TARGET_SYNTH3_CUTOFF,
    MOTION_TARGET_SYNTH1_RESONANCE,
    MOTION_TARGET_SYNTH2_RESONANCE,
    MOTION_TARGET_SYNTH3_RESONANCE,
    MOTION_TARGET_COUNT,
};

enum MotionGesture : uint8_t {
    MOTION_GESTURE_SHAKE = 1u << 0,
    MOTION_GESTURE_SLAP = 1u << 1,
    MOTION_GESTURE_MOVE = 1u << 2,
};

struct MotionInput {
    float ax, ay, az;
    float gx, gy, gz;
    uint32_t timeMs;
};

struct MotionOutput {
    uint8_t values[MOTION_SOURCE_COUNT];
    uint8_t gestures;
};

struct MotionMapping {
    uint8_t source;
    uint8_t target;
};

class MotionFilter {
public:
    MotionFilter() { reset(); }

    void reset() {
        _initialized = false;
        _lastTimeMs = 0;
        _lastShakeMs = 0;
        _lastSlapMs = 0;
        _fax = _fay = _faz = 0.0f;
        _fgx = _fgy = _fgz = 0.0f;
        _previousMagnitude = 1.0f;
    }

    MotionOutput update(const MotionInput& input) {
        const float alpha = 0.18f;
        if (!_initialized) {
            _fax = input.ax; _fay = input.ay; _faz = input.az;
            _fgx = input.gx; _fgy = input.gy; _fgz = input.gz;
            _previousMagnitude = magnitude(input.ax, input.ay, input.az);
            _initialized = true;
        } else {
            _fax += alpha * (input.ax - _fax);
            _fay += alpha * (input.ay - _fay);
            _faz += alpha * (input.az - _faz);
            _fgx += alpha * (input.gx - _fgx);
            _fgy += alpha * (input.gy - _fgy);
            _fgz += alpha * (input.gz - _fgz);
        }

        const float accelMagnitude = magnitude(input.ax, input.ay, input.az);
        const float gyroMagnitude = magnitude(_fgx, _fgy, _fgz);
        const float jerk = fabsf(accelMagnitude - _previousMagnitude);
        _previousMagnitude = accelMagnitude;

        MotionOutput output = {};
        output.values[MOTION_SOURCE_TILT_X] = bipolarToMidi(
            atan2f(_fay, sqrtf(_fax * _fax + _faz * _faz)) * 57.2957795f);
        output.values[MOTION_SOURCE_TILT_Y] = bipolarToMidi(
            atan2f(-_fax, sqrtf(_fay * _fay + _faz * _faz)) * 57.2957795f);
        output.values[MOTION_SOURCE_ACCEL] = unipolarToMidi(fabsf(accelMagnitude - 1.0f), 2.0f);
        output.values[MOTION_SOURCE_GYRO] = unipolarToMidi(gyroMagnitude, 500.0f);

        const bool shake = fabsf(accelMagnitude - 1.0f) > 0.45f &&
                           input.timeMs - _lastShakeMs >= 180;
        const bool slap = jerk > 1.20f && input.timeMs - _lastSlapMs >= 250;
        if (shake) {
            output.gestures |= MOTION_GESTURE_SHAKE;
            output.values[MOTION_SOURCE_SHAKE] = 127;
            _lastShakeMs = input.timeMs;
        }
        if (slap) {
            output.gestures |= MOTION_GESTURE_SLAP;
            output.values[MOTION_SOURCE_SLAP] = 127;
            _lastSlapMs = input.timeMs;
        }
        if (gyroMagnitude > 80.0f) output.gestures |= MOTION_GESTURE_MOVE;
        _lastTimeMs = input.timeMs;
        return output;
    }

private:
    bool _initialized;
    uint32_t _lastTimeMs, _lastShakeMs, _lastSlapMs;
    float _fax, _fay, _faz, _fgx, _fgy, _fgz;
    float _previousMagnitude;

    static float magnitude(float x, float y, float z) {
        return sqrtf(x * x + y * y + z * z);
    }
    static uint8_t bipolarToMidi(float degrees) {
        float normalized = 0.5f + degrees / 90.0f;
        if (normalized < 0.0f) normalized = 0.0f;
        else if (normalized > 1.0f) normalized = 1.0f;
        return static_cast<uint8_t>(normalized * 127.0f + 0.5f);
    }
    static uint8_t unipolarToMidi(float value, float maximum) {
        float normalized = value / maximum;
        if (normalized < 0.0f) normalized = 0.0f;
        else if (normalized > 1.0f) normalized = 1.0f;
        return static_cast<uint8_t>(normalized * 127.0f + 0.5f);
    }
};

