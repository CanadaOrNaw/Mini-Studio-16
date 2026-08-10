#include "../loop_stream_core.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

using TestCore = LoopStreamCore<16, 32>;

static void fillPlayback(TestCore& core, uint8_t track, int16_t first, size_t count) {
    std::vector<int16_t> frames(count);
    for (size_t index = 0; index < count; ++index)
        frames[index] = static_cast<int16_t>(first + index);
    assert(core.pushPlayback(track, frames.data(), frames.size()) == frames.size());
}

int main() {
    TestCore core;
    assert(core.establishTimeline(8));
    assert(TestCore::nextBoundary(0, 8, false) == 0);
    assert(TestCore::nextBoundary(0, 8, true) == 8);
    assert(TestCore::nextBoundary(11, 8, true) == 16);

    // A primed track begins on its exact scheduled audio frame.
    assert(core.preparePlayback(0, 8));
    fillPlayback(core, 0, 100, 8);
    assert(core.armPlayback(0, 4));
    for (int frame = 0; frame < 4; ++frame) assert(core.processFrame(0) == 0);
    assert(core.processFrame(0) == 100);
    assert(core.snapshot(0).state == LOOP_STREAM_PLAYING);

    // Mute consumes buffered frames, so unmute returns at the correct phase.
    assert(core.setMuted(0, true));
    assert(core.processFrame(0) == 0);
    assert(core.setMuted(0, false));
    assert(core.processFrame(0) == 102);

    // A storage stall is counted and moves the track into an explicit resync
    // state instead of letting delayed samples shift its musical phase.
    for (int frame = 0; frame < 6; ++frame) core.processFrame(0);
    assert(core.snapshot(0).state == LOOP_STREAM_UNDERRUN);
    assert(core.snapshot(0).underruns == 1);
    assert(core.prepareResync(0));
    fillPlayback(core, 0, 200, 8);
    const uint32_t restart = TestCore::nextBoundary(core.absoluteFrame(), 8, true);
    assert(core.armPlayback(0, restart));
    while (core.absoluteFrame() < restart) assert(core.processFrame(0) == 0);
    assert(core.processFrame(0) == 200);

    // Track 1 free recording establishes the timeline outside the audio core.
    TestCore record;
    assert(record.beginRecording(0, 3, 0));
    for (int frame = 0; frame < 3; ++frame) record.processFrame(10 + frame);
    assert(record.snapshot(0).capturedFrames == 0);
    for (int frame = 0; frame < 5; ++frame) record.processFrame(20 + frame);
    assert(record.requestStopRecording(0));
    int16_t captured[8] = {};
    assert(record.popRecorded(captured, 8) == 5);
    assert(captured[0] == 20 && captured[4] == 24);
    assert(record.establishTimeline(5));
    assert(record.completeRecording(0, 5));

    // A later track waits for the next common boundary and auto-finalizes at
    // exactly Track 1's frame count.
    assert(record.beginRecording(1, 10, 5));
    while (record.absoluteFrame() < 10) record.processFrame(0);
    for (int frame = 0; frame < 5; ++frame) record.processFrame(30 + frame);
    assert(record.snapshot(1).state == LOOP_STREAM_FINALIZING);
    assert(record.snapshot(1).capturedFrames == 5);
    assert(record.popRecorded(captured, 8) == 5);

    // A deliberately stalled storage consumer cannot corrupt memory: excess
    // frames are counted and make the take rejectable by the file layer.
    LoopStreamCore<8, 4> overrun;
    assert(overrun.beginRecording(0, 0, 0));
    for (int frame = 0; frame < 9; ++frame) overrun.processFrame(frame);
    assert(overrun.snapshot(0).capturedFrames == 9);
    assert(overrun.snapshot(0).droppedFrames == 5);

    std::cout << "loop_stream_core: scheduling, stall recovery and recording passed\n";
    return 0;
}
