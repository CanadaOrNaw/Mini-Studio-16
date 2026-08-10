#include "../sample_stream_core.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    SampleStreamCore<2, 16> core;
    const int16_t ramp[] = {0, 1000, 2000, 3000, 4000, 5000, 6000, 7000};
    const uint8_t voice = core.allocateVoice();
    assert(core.prepare(voice, 3, 8, 32768, 32767));  // half-speed interpolation
    assert(core.push(voice, ramp, 8) == 8);
    core.markEof(voice);
    assert(core.arm(voice, 4));
    assert(core.render() == 0);
    assert(core.render() == 500);
    assert(core.render() == 1000);
    assert(core.render() == 1500);
    assert(core.snapshot(voice).consumedFrames == 2);

    // A high-pitch voice consumes four source frames per output frame.
    const uint8_t fast = core.allocateVoice();
    assert(fast != voice);
    assert(core.prepare(fast, 4, 8, 4 * 65536u, 32767));
    assert(core.push(fast, ramp, 8) == 8);
    core.markEof(fast);
    assert(core.arm(fast, 4));
    core.render();
    assert(core.snapshot(fast).consumedFrames == 4);

    // Without EOF, exhausting a producer ring is a counted underrun, never an
    // out-of-bounds read or an unbounded wait in the renderer.
    SampleStreamCore<1, 4> stalled;
    assert(stalled.prepare(0, 1, 10, 65536, 32767));
    assert(stalled.push(0, ramp, 2) == 2);
    assert(stalled.arm(0, 2));
    stalled.render();
    stalled.render();
    assert(stalled.snapshot(0).state == SAMPLE_STREAM_UNDERRUN);
    assert(stalled.snapshot(0).underruns == 1);

    // With all voices active, allocation deterministically steals the oldest.
    SampleStreamCore<2, 4> stealing;
    assert(stealing.prepare(0, 0, 8, 65536, 32767));
    assert(stealing.prepare(1, 1, 8, 65536, 32767));
    assert(stealing.allocateVoice() == 0);

    std::cout << "sample_stream_core: pitch, EOF, underrun and stealing passed\n";
    return 0;
}
