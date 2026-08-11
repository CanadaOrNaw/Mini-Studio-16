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

    // P3 regression: with current below loopStart (a MIDI Song Position
    // seek), the walk must still reach the loop region instead of running
    // out of attempts and freezing the transport.
    uint8_t seeked[128];
    for (int index = 0; index < 128; ++index) seeked[index] = 0xFF;
    seeked[100] = 3;
    seeked[110] = 4;
    assert(songChainNext(seeked, 128, 0, 100, 0xFF, position, pattern));
    assert(position == 100 && pattern == 3);
    assert(songChainNext(seeked, 128, 100, 100, 0xFF, position, pattern));
    assert(position == 110 && pattern == 4);

    std::cout << "song_chain: start, sparse advance and loop wrap passed\n";
    return 0;
}
