#pragma once

#include <stdint.h>

enum MidiEventType : uint8_t {
    MIDI_EVENT_NONE = 0,
    MIDI_EVENT_NOTE_ON,
    MIDI_EVENT_NOTE_OFF,
    MIDI_EVENT_CC,
    MIDI_EVENT_CLOCK,
    MIDI_EVENT_START,
    MIDI_EVENT_CONTINUE,
    MIDI_EVENT_STOP,
    MIDI_EVENT_SONG_POSITION,
};

struct MidiEvent {
    MidiEventType type;
    uint8_t channel;
    uint8_t data1;
    uint8_t data2;
    uint16_t value14;
};

class MidiParser {
public:
    MidiParser() { reset(); }
    void reset();
    bool feed(uint8_t byte, MidiEvent& event);

private:
    uint8_t _status;
    uint8_t _runningStatus;
    uint8_t _data[2];
    uint8_t _count;
    uint8_t _expected;
    bool _sysex;

    void startStatus(uint8_t status);
    bool emitMessage(MidiEvent& event);
};
