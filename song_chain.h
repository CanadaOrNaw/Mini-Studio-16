#pragma once

#include <stddef.h>
#include <stdint.h>

// Resolve the first playable entry at/after the loop point. Empty slots before
// the loop point are intentionally excluded: FROM TOP means the configured
// song start, not array index zero.
inline bool songChainFirst(const uint8_t* song, size_t length, uint8_t loopStart,
                           uint8_t empty, uint8_t& position, uint8_t& pattern) {
    if (!song || length == 0 || length > 256 || loopStart >= length) return false;
    for (size_t offset = 0; offset < length - loopStart; ++offset) {
        const size_t cursor = static_cast<size_t>(loopStart) + offset;
        if (song[cursor] == empty) continue;
        position = static_cast<uint8_t>(cursor);
        pattern = song[cursor];
        return true;
    }
    return false;
}

// Resolve the next non-empty entry and wrap to loopStart. Outputs are only
// changed on success so an entirely empty loop region leaves transport stable.
inline bool songChainNext(const uint8_t* song, size_t length, uint8_t current,
                          uint8_t loopStart, uint8_t empty,
                          uint8_t& position, uint8_t& pattern) {
    if (!song || length == 0 || length > 256 || current >= length ||
        loopStart >= length)
        return false;
    size_t cursor = current;
    // P3 (reconciliation report): bound the walk by the full chain length,
    // not the loop-region size — when current sits below loopStart (e.g.
    // after a MIDI Song Position seek), length-loopStart attempts could be
    // exhausted before the cursor ever reached the loop region, freezing
    // the transport on a chain that has playable entries.
    const size_t searchLength = length;
    for (size_t attempt = 0; attempt < searchLength; ++attempt) {
        ++cursor;
        if (cursor >= length) cursor = loopStart;
        if (song[cursor] == empty) continue;
        position = static_cast<uint8_t>(cursor);
        pattern = song[cursor];
        return true;
    }
    return false;
}
