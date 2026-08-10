#include "../song_chain.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    uint8_t song[128];
    std::memset(song, 0xFF, sizeof(song));
    uint8_t position = 99;
    uint8_t pattern = 99;

    assert(!songChainFirst(song, 128, 0, 0xFF, position, pattern));
    assert(position == 99 && pattern == 99);

    song[10] = 3;
    song[12] = 5;
    assert(songChainFirst(song, 128, 10, 0xFF, position, pattern));
    assert(position == 10 && pattern == 3);
    assert(songChainNext(song, 128, 10, 10, 0xFF, position, pattern));
    assert(position == 12 && pattern == 5);

    // Regression: advancing from entry 127 must wrap to the configured loop
    // point, never index zero.
    song[127] = 7;
    song[0] = 9;
    assert(songChainNext(song, 128, 127, 10, 0xFF, position, pattern));
    assert(position == 10 && pattern == 3);

    std::memset(song, 0xFF, sizeof(song));
    song[127] = 6;
    assert(songChainFirst(song, 128, 100, 0xFF, position, pattern));
    assert(position == 127 && pattern == 6);
    assert(songChainNext(song, 128, 127, 100, 0xFF, position, pattern));
    assert(position == 127 && pattern == 6);

    assert(!songChainNext(nullptr, 128, 0, 0, 0xFF, position, pattern));
    assert(!songChainNext(song, 128, 128, 0, 0xFF, position, pattern));

    std::cout << "song_chain: start, sparse advance and loop wrap passed\n";
    return 0;
}
