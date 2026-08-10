#include "../motion_core.h"

#include <cassert>
#include <iostream>

int main() {
    MotionFilter filter;
    MotionOutput output = filter.update({0, 0, 1, 0, 0, 0, 1000});
    assert(output.values[MOTION_SOURCE_TILT_X] >= 63 &&
           output.values[MOTION_SOURCE_TILT_X] <= 64);
    assert(output.gestures == 0);

    for (uint32_t time = 1010; time < 1210; time += 10)
        output = filter.update({0, 0.707f, 0.707f, 0, 0, 0, time});
    assert(output.values[MOTION_SOURCE_TILT_X] > 115);

    output = filter.update({2.0f, 0, 0, 1000, 0, 0, 1300});
    assert(output.gestures & MOTION_GESTURE_SHAKE);
    assert(output.gestures & MOTION_GESTURE_MOVE);
    assert(output.values[MOTION_SOURCE_SHAKE] == 127);

    output = filter.update({-4.0f, 0, 0, 0, 0, 0, 1310});
    assert((output.gestures & MOTION_GESTURE_SHAKE) == 0);  // cooldown
    assert(output.gestures & MOTION_GESTURE_SLAP);

    std::cout << "motion_core: filtering, tilt and gesture cooldown passed\n";
    return 0;
}
