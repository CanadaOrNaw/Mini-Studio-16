#include "../event_looper_core.h"

#include <cassert>
#include <iostream>

int main() {
    EventLooperCore looper;
    assert(looper.setBars(EVENT_ROLE_BASS, 128));
    assert(looper.bars(EVENT_ROLE_BASS) == 128);
    assert(looper.setArmed(EVENT_ROLE_BASS, true));
    assert(looper.add(2047, EVENT_ROLE_BASS, EVENT_LOOP_NOTE, 0, 60, 100));
    assert(looper.add(2047, EVENT_ROLE_BASS, EVENT_LOOP_NOTE, 0, 60, 0,
                      EVENT_LOOP_FLAG_NOTE_OFF));
    assert(looper.count(EVENT_ROLE_BASS) == 2);

    int fired = 0;
    looper.forStep(2047, [&](const EventLoopEvent& event) {
        ++fired; assert(event.value1 == 60);
    });
    looper.forStep(4095, [&](const EventLoopEvent&) { ++fired; });
    assert(fired == 4);

    assert(looper.setMuted(EVENT_ROLE_BASS, true));
    looper.forStep(2047, [&](const EventLoopEvent&) { ++fired; });
    assert(fired == 4);
    assert(looper.setMuted(EVENT_ROLE_BASS, false));

    assert(looper.setBars(EVENT_ROLE_DRUM, 2));
    assert(looper.setArmed(EVENT_ROLE_DRUM, true));
    assert(looper.add(35, EVENT_ROLE_DRUM, EVENT_LOOP_DRUM, 3, 127, 0));
    assert(looper.event(2).step == 3);
    assert(looper.clearTrack(EVENT_ROLE_DRUM));
    assert(looper.count() == 2);
    assert(!looper.add(0, EVENT_ROLE_BASS, EVENT_LOOP_NOTE, 0, 60, 0, 2));
    assert(!looper.add(0, EVENT_ROLE_BASS, EVENT_LOOP_DRUM, 0, 127, 0,
                       EVENT_LOOP_FLAG_NOTE_OFF));

    EventLoopEvent invalid = {2048, EVENT_ROLE_BASS, EVENT_LOOP_NOTE, 0, 1, 2, 0};
    assert(!looper.appendLoaded(invalid));
    assert(!looper.setBars(0, 129));

    EventLooperCore capacity;
    assert(capacity.setArmed(0, true));
    for (uint16_t i = 0; i < EVENT_LOOP_CAPACITY; ++i)
        assert(capacity.add(i, 0, EVENT_LOOP_DRUM, 0, 127, 0));
    assert(!capacity.add(0, 0, EVENT_LOOP_DRUM, 0, 127, 0));

    // P3 regression: shrinking a track re-wraps its events into the new
    // length instead of orphaning them beyond it.
    EventLooperCore shrink;
    assert(shrink.setArmed(0, true));
    assert(shrink.setBars(0, 4));   // 64 steps
    assert(shrink.add(3, 0, EVENT_LOOP_DRUM, 0, 127, 0));
    assert(shrink.add(50, 0, EVENT_LOOP_DRUM, 1, 127, 0));
    assert(shrink.setBars(0, 2));   // 32 steps: step 50 must become 18
    bool sawWrapped = false;
    shrink.forStep(18, [&](const EventLoopEvent& event) {
        if (event.target == 1) sawWrapped = true;
    });
    assert(sawWrapped);
    bool sawOriginal = false;
    shrink.forStep(3, [&](const EventLoopEvent& event) {
        if (event.target == 0) sawOriginal = true;
    });
    assert(sawOriginal);

    std::cout << "event_looper: 5 tracks, 128 bars and capacity passed\n";
    return 0;
}
