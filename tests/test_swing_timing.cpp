#include "../swing_timing.h"
#include <assert.h>
int main() {
    assert(swingStepPeriod(1000, 50, 0) == 1000);
    assert(swingStepPeriod(1000, 50, 1) == 1000);
    assert(swingStepPeriod(1000, 66, 0) == 1320);
    assert(swingStepPeriod(1000, 66, 1) == 680);
    assert(swingStepPeriod(1000, 75, 0) + swingStepPeriod(1000, 75, 1) == 2000);
    assert(swingStepPeriod(1000, 99, 0) == 1500);
    return 0;
}
