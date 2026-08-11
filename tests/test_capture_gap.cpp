#include "../capture_gap.h"
#include "../pcm_ring.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

int main() {
    SpscRing<int16_t, 4> ring;
    uint32_t gap = 0;
    const int16_t first[] = {1, 2, 3, 4};
    const int16_t overflow[] = {5, 6};
    const int16_t whileGap[] = {7, 8};
    const int16_t recovered[] = {9, 10};

    assert(capturePushWithGap(ring, &gap, first, 4) == 4);
    assert(capturePushWithGap(ring, &gap, overflow, 2) == 0);
    assert(gap == 2);

    int16_t out[4] = {};
    assert(ring.pop(out, 2) == 2);
    assert(out[0] == 1 && out[1] == 2);
    // Space is available now, but accepting newer audio would put it ahead
    // of the missing frames. It must extend the pending silence instead.
    assert(capturePushWithGap(ring, &gap, whileGap, 2) == 0);
    assert(gap == 4);
    assert(ring.pop(out, 4) == 2);
    assert(out[0] == 3 && out[1] == 4);

    std::vector<int16_t> timeline = {1, 2, 3, 4};
    timeline.insert(timeline.end(), captureTakeGap(&gap), 0);
    assert(gap == 0);
    assert(capturePushWithGap(ring, &gap, recovered, 2) == 2);
    assert(ring.pop(out, 2) == 2);
    timeline.push_back(out[0]);
    timeline.push_back(out[1]);

    const std::vector<int16_t> expected = {1, 2, 3, 4, 0, 0, 0, 0, 9, 10};
    assert(timeline == expected);
    std::cout << "capture_gap: overrun silence ordering passed\n";
    return 0;
}
