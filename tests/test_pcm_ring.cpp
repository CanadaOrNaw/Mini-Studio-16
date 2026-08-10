#include "../pcm_ring.h"

#include <atomic>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <thread>

static void testBasicAndWrap() {
    SpscRing<int16_t, 8> ring;
    assert(ring.empty());
    assert(ring.capacity() == 8);

    const int16_t first[] = {1, 2, 3, 4, 5, 6};
    assert(ring.push(first, 6) == 6);
    assert(ring.size() == 6);

    int16_t out[8] = {};
    assert(ring.pop(out, 4) == 4);
    for (int i = 0; i < 4; ++i) assert(out[i] == i + 1);

    const int16_t second[] = {7, 8, 9, 10, 11, 12};
    assert(ring.push(second, 6) == 6);
    assert(ring.full());
    assert(!ring.pushOne(second[0]));

    assert(ring.pop(out, 8) == 8);
    const int16_t expected[] = {5, 6, 7, 8, 9, 10, 11, 12};
    for (int i = 0; i < 8; ++i) assert(out[i] == expected[i]);
    assert(ring.empty());
    assert(!ring.popOne(out[0]));
}

static void testBulkBoundsAndDiscard() {
    SpscRing<uint32_t, 4> ring;
    const uint32_t values[] = {10, 20, 30, 40, 50};
    assert(ring.push(values, 5) == 4);
    assert(ring.discard(2) == 2);
    assert(ring.size() == 2);
    uint32_t out[4] = {};
    assert(ring.pop(out, 4) == 2);
    assert(out[0] == 30 && out[1] == 40);
    assert(ring.discard(1) == 0);
    ring.reset();
    assert(ring.empty() && ring.freeSpace() == 4);
}

static void testConcurrentOrder() {
    constexpr uint32_t total = 1000000;
    SpscRing<uint32_t, 1024> ring;
    std::atomic<bool> start{false};

    std::thread producer([&] {
        while (!start.load(std::memory_order_acquire)) {}
        for (uint32_t value = 0; value < total;) {
            if (ring.pushOne(value)) ++value;
            else std::this_thread::yield();
        }
    });

    std::thread consumer([&] {
        start.store(true, std::memory_order_release);
        for (uint32_t expected = 0; expected < total;) {
            uint32_t value = 0;
            if (ring.popOne(value)) {
                assert(value == expected);
                ++expected;
            } else std::this_thread::yield();
        }
    });

    producer.join();
    consumer.join();
    assert(ring.empty());
}

int main() {
    {
        SpscRing<int, 8> peekRing;
        const int values[] = {4, 5, 6};
        assert(peekRing.push(values, 3) == 3);
        int value = 0;
        assert(peekRing.peek(0, value) && value == 4);
        assert(peekRing.peek(2, value) && value == 6);
        assert(!peekRing.peek(3, value));
        assert(peekRing.size() == 3);
    }
    testBasicAndWrap();
    testBulkBoundsAndDiscard();
    testConcurrentOrder();
    std::cout << "pcm_ring: all tests passed\n";
    return 0;
}
