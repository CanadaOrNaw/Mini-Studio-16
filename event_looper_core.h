#pragma once

#include <stddef.h>
#include <stdint.h>

static const uint8_t EVENT_LOOP_TRACKS = 5;
static const uint16_t EVENT_LOOP_MAX_BARS = 128;
static const uint16_t EVENT_LOOP_STEPS_PER_BAR = 16;
static const uint16_t EVENT_LOOP_MAX_STEPS =
    EVENT_LOOP_MAX_BARS * EVENT_LOOP_STEPS_PER_BAR;
static const uint16_t EVENT_LOOP_CAPACITY = 2048;

enum EventLoopType : uint8_t {
    EVENT_LOOP_NOTE = 1,
    EVENT_LOOP_DRUM,
    EVENT_LOOP_SAMPLE,
    EVENT_LOOP_CONTROL,
};

enum EventLoopRole : uint8_t {
    EVENT_ROLE_DRUM = 0,
    EVENT_ROLE_BASS,
    EVENT_ROLE_CHORD,
    EVENT_ROLE_LEAD,
    EVENT_ROLE_SAMPLE,
};

struct __attribute__((packed)) EventLoopEvent {
    uint16_t step;
    uint8_t track;
    uint8_t type;
    uint8_t target;
    uint8_t value1;
    uint8_t value2;
    uint8_t flags;
};

struct EventLoopTrackState {
    uint8_t bars;
    bool armed;
    bool muted;
};

class EventLooperCore {
public:
    EventLooperCore() { clearAll(); }

    void clearAll() {
        _count = 0;
        for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
            _tracks[track].bars = 1;
            _tracks[track].armed = false;
            _tracks[track].muted = false;
        }
    }

    bool setBars(uint8_t track, uint16_t bars) {
        if (track >= EVENT_LOOP_TRACKS || bars == 0 || bars > EVENT_LOOP_MAX_BARS)
            return false;
        _tracks[track].bars = static_cast<uint8_t>(bars == 128 ? 0 : bars);
        return true;
    }

    uint16_t bars(uint8_t track) const {
        if (track >= EVENT_LOOP_TRACKS) return 0;
        return _tracks[track].bars == 0 ? 128 : _tracks[track].bars;
    }

    bool setArmed(uint8_t track, bool armed) {
        if (track >= EVENT_LOOP_TRACKS) return false;
        _tracks[track].armed = armed;
        return true;
    }

    bool setMuted(uint8_t track, bool muted) {
        if (track >= EVENT_LOOP_TRACKS) return false;
        _tracks[track].muted = muted;
        return true;
    }

    bool add(uint16_t absoluteStep, uint8_t track, EventLoopType type,
             uint8_t target, uint8_t value1, uint8_t value2, uint8_t flags = 0) {
        if (track >= EVENT_LOOP_TRACKS || !_tracks[track].armed ||
            type < EVENT_LOOP_NOTE || type > EVENT_LOOP_CONTROL ||
            _count >= EVENT_LOOP_CAPACITY)
            return false;
        EventLoopEvent event = {};
        const uint16_t length = bars(track) * EVENT_LOOP_STEPS_PER_BAR;
        event.step = static_cast<uint16_t>(absoluteStep % length);
        event.track = track;
        event.type = type;
        event.target = target;
        event.value1 = value1;
        event.value2 = value2;
        event.flags = flags;
        _events[_count++] = event;
        return true;
    }

    bool appendLoaded(const EventLoopEvent& event) {
        if (_count >= EVENT_LOOP_CAPACITY || event.track >= EVENT_LOOP_TRACKS ||
            event.type < EVENT_LOOP_NOTE || event.type > EVENT_LOOP_CONTROL ||
            event.step >= bars(event.track) * EVENT_LOOP_STEPS_PER_BAR)
            return false;
        _events[_count++] = event;
        return true;
    }

    bool clearTrack(uint8_t track) {
        if (track >= EVENT_LOOP_TRACKS) return false;
        uint16_t write = 0;
        for (uint16_t read = 0; read < _count; ++read)
            if (_events[read].track != track) _events[write++] = _events[read];
        _count = write;
        return true;
    }

    template <typename Callback>
    void forStep(uint16_t absoluteStep, Callback callback) const {
        for (uint16_t index = 0; index < _count; ++index) {
            const EventLoopEvent& event = _events[index];
            const EventLoopTrackState& track = _tracks[event.track];
            if (track.muted) continue;
            const uint16_t length = bars(event.track) * EVENT_LOOP_STEPS_PER_BAR;
            if (event.step == absoluteStep % length) callback(event);
        }
    }

    uint16_t count() const { return _count; }
    uint16_t count(uint8_t track) const {
        if (track >= EVENT_LOOP_TRACKS) return 0;
        uint16_t result = 0;
        for (uint16_t index = 0; index < _count; ++index)
            if (_events[index].track == track) ++result;
        return result;
    }
    const EventLoopEvent& event(uint16_t index) const { return _events[index]; }
    const EventLoopTrackState& track(uint8_t index) const { return _tracks[index]; }

private:
    EventLoopEvent _events[EVENT_LOOP_CAPACITY];
    EventLoopTrackState _tracks[EVENT_LOOP_TRACKS];
    uint16_t _count;
};

static_assert(sizeof(EventLoopEvent) == 8, "event loop storage layout changed");

