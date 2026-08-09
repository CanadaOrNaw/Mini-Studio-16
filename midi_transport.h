#pragma once

#include "midi_parser.h"

#include <stdint.h>

enum MidiTransportAction : uint8_t {
    MIDI_TRANSPORT_NONE = 0,
    MIDI_TRANSPORT_START,
    MIDI_TRANSPORT_CONTINUE,
    MIDI_TRANSPORT_STOP,
    MIDI_TRANSPORT_STEP,
    MIDI_TRANSPORT_SEEK,
};

struct MidiTransportResult {
    MidiTransportAction action;
    uint16_t songPosition;
};

class MidiTransportClock {
public:
    MidiTransportClock() : _running(false), _clockInStep(0) {}
    MidiTransportResult consume(const MidiEvent& event);
    bool running() const { return _running; }
    uint8_t clockInStep() const { return _clockInStep; }

private:
    bool _running;
    uint8_t _clockInStep;
};
