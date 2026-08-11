#pragma once

#include <stddef.h>
#include <stdint.h>

// Preserve the position of an audio-capture overrun without blocking the
// real-time producer. Once a ring first overflows, later frames are counted
// as part of the same silence gap until the storage consumer has drained the
// older real frames and claimed that gap. This prevents newer audio from
// being written before silence that belongs earlier in the timeline.
//
// One producer and one consumer are required, matching the recorder rings.
template <typename Ring, typename Frame>
size_t capturePushWithGap(Ring& ring, uint32_t* pendingSilence,
                          const Frame* frames, size_t count) {
    if (!pendingSilence || !frames || count == 0) return 0;
    if (__atomic_load_n(pendingSilence, __ATOMIC_ACQUIRE) != 0) {
        __atomic_add_fetch(pendingSilence, static_cast<uint32_t>(count),
                           __ATOMIC_ACQ_REL);
        return 0;
    }
    const size_t pushed = ring.push(frames, count);
    if (pushed < count)
        __atomic_add_fetch(pendingSilence,
                           static_cast<uint32_t>(count - pushed),
                           __ATOMIC_ACQ_REL);
    return pushed;
}

inline uint32_t captureTakeGap(uint32_t* pendingSilence) {
    return pendingSilence
        ? __atomic_exchange_n(pendingSilence, 0u, __ATOMIC_ACQ_REL) : 0u;
}
