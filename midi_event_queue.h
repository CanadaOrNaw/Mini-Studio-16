#pragma once

#include "midi_parser.h"
#include "pcm_ring.h"

template <uint32_t Capacity>
class MidiEventQueue {
public:
    MidiEventQueue() : _dropped(0) {}
    bool push(const MidiEvent& event) {
        if (_ring.pushOne(event)) return true;
        __atomic_add_fetch(&_dropped, 1u, __ATOMIC_RELAXED);
        return false;
    }
    bool pop(MidiEvent& event) { return _ring.popOne(event); }
    uint32_t size() const { return _ring.size(); }
    uint32_t dropped() const { return __atomic_load_n(&_dropped, __ATOMIC_RELAXED); }
    void reset() { _ring.reset(); __atomic_store_n(&_dropped, 0u, __ATOMIC_RELAXED); }

private:
    SpscRing<MidiEvent, Capacity> _ring;
    alignas(4) uint32_t _dropped;
};
