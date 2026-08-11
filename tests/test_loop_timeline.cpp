#include "../loop_timeline.h"

#include <cassert>
#include <iostream>

int main() {
    LoopTimeline timeline;
    LoopTrackControl track1(0);
    assert(track1.arm());
    assert(track1.requestRecord(100, timeline));
    assert(track1.state() == LOOP_TRACK_RECORDING);
    assert(track1.finishRecording(441000, timeline));
    assert(timeline.lengthFrames() == 441000);
    assert(track1.lengthFrames() == 441000);

    LoopTrackControl track2(1);
    assert(track2.arm());
    assert(track2.requestRecord(500000, timeline));
    assert(track2.state() == LOOP_TRACK_WAITING);
    assert(track2.scheduledFrame() == 882000);
    assert(!track2.update(881999));
    assert(track2.update(882000));
    assert(track2.finishRecording(450000, timeline));
    assert(track2.lengthFrames() == timeline.lengthFrames());
    assert(track2.toggleMute() && track2.state() == LOOP_TRACK_MUTED);
    assert(track2.toggleMute() && track2.state() == LOOP_TRACK_PLAYING);

    assert(timeline.position(882123) == 123);
    assert(timeline.framesUntilBoundary(882000) == 0);
    std::cout << "loop_timeline: sync and state tests passed\n";
    return 0;
}
