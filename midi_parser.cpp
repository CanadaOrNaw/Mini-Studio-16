#include "midi_parser.h"

#include <string.h>

void MidiParser::reset() {
    _status = 0;
    _runningStatus = 0;
    _data[0] = _data[1] = 0;
    _count = 0;
    _expected = 0;
    _sysex = false;
}

void MidiParser::startStatus(uint8_t status) {
    _status = status;
    _count = 0;
    if (status >= 0x80 && status <= 0xEF) {
        _runningStatus = status;
        const uint8_t kind = status & 0xF0;
        _expected = (kind == 0xC0 || kind == 0xD0) ? 1 : 2;
    } else {
        _runningStatus = 0;
        _expected = status == 0xF2 ? 2 : (status == 0xF1 || status == 0xF3 ? 1 : 0);
    }
}

bool MidiParser::emitMessage(MidiEvent& event) {
    memset(&event, 0, sizeof(event));
    const uint8_t kind = _status & 0xF0;
    event.channel = _status & 0x0F;
    event.data1 = _data[0];
    event.data2 = _data[1];

    if (_status == 0xF2) {
        event.type = MIDI_EVENT_SONG_POSITION;
        event.value14 = static_cast<uint16_t>(_data[0] | (_data[1] << 7));
    } else if (kind == 0x80) {
        event.type = MIDI_EVENT_NOTE_OFF;
    } else if (kind == 0x90) {
        event.type = _data[1] == 0 ? MIDI_EVENT_NOTE_OFF : MIDI_EVENT_NOTE_ON;
    } else if (kind == 0xB0) {
        event.type = MIDI_EVENT_CC;
    } else {
        event.type = MIDI_EVENT_NONE;
    }

    _count = 0;
    if (_status >= 0xF0) {
        _status = 0;
        _expected = 0;
    }
    return event.type != MIDI_EVENT_NONE;
}

bool MidiParser::feed(uint8_t byte, MidiEvent& event) {
    memset(&event, 0, sizeof(event));

    // System realtime may appear between data bytes and never disturbs running status.
    switch (byte) {
        case 0xF8: event.type = MIDI_EVENT_CLOCK; return true;
        case 0xFA: event.type = MIDI_EVENT_START; return true;
        case 0xFB: event.type = MIDI_EVENT_CONTINUE; return true;
        case 0xFC: event.type = MIDI_EVENT_STOP; return true;
        default: break;
    }
    // P2-1 (reconciliation report): ALL 0xF8-0xFF bytes are system realtime
    // and must be fully transparent per the MIDI spec. 0xFE (Active
    // Sensing, sent every ~300 ms by common keyboards), 0xFF (Reset), 0xF9
    // and 0xFD previously fell through to startStatus(), aborting the
    // in-flight message and cancelling running status — dropping notes.
    if (byte >= 0xF8) return false;

    if (byte & 0x80) {
        if (byte == 0xF0) { _sysex = true; _runningStatus = 0; _count = 0; return false; }
        if (byte == 0xF7) { _sysex = false; _status = 0; _count = 0; return false; }
        if (_sysex) return false;
        startStatus(byte);
        return false;
    }

    if (_sysex) return false;
    if (_expected == 0) {
        if (_runningStatus == 0) return false;
        startStatus(_runningStatus);
    }
    if (_count < sizeof(_data)) _data[_count++] = byte & 0x7F;
    if (_count < _expected) return false;
    return emitMessage(event);
}
