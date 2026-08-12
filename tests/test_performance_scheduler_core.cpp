#include "../performance_scheduler_core.h"
#include <assert.h>
#include <stdint.h>

int main() {
    PerformanceGateRelease gate = performanceGateMake(2, 127, 4, true,
                                                       100350000u);
    assert(performanceGateTrack(gate) == 2);
    assert(performanceGateNote(gate) == 127);
    assert(performanceGateRole(gate) == 4);
    assert(performanceGateAudition(gate));
    assert(!performanceGateDue(gate, 100349000u));
    assert(performanceGateDue(gate, 100350000u));

    // The deadline wraps from 65,535 ms to 0 ms; a short gate remains ordered.
    gate = performanceGateMake(1, 60, 3, false, 65546000u);
    assert(!performanceGateDue(gate, 65535000u));
    assert(performanceGateDue(gate, 65546000u));
    assert(performanceGateDue(gate, 65550000u));
    assert(!performanceGateAudition(gate));
    return 0;
}
