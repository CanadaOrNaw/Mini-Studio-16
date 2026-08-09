#include "midi_transport.h"

MidiTransportResult MidiTransportClock::consume(const MidiEvent& event) {
    MidiTransportResult result = {MIDI_TRANSPORT_NONE, 0};
    switch (event.type) {
        case MIDI_EVENT_START:
            _running = true;
            _clockInStep = 0;
            result.action = MIDI_TRANSPORT_START;
            break;
        case MIDI_EVENT_CONTINUE:
            _running = true;
            result.action = MIDI_TRANSPORT_CONTINUE;
            break;
        case MIDI_EVENT_STOP:
            _running = false;
            result.action = MIDI_TRANSPORT_STOP;
            break;
        case MIDI_EVENT_SONG_POSITION:
            _clockInStep = 0;
            result.action = MIDI_TRANSPORT_SEEK;
            result.songPosition = event.value14;
            break;
        case MIDI_EVENT_CLOCK:
            if (_running) {
                if (_clockInStep == 0) result.action = MIDI_TRANSPORT_STEP;
                _clockInStep = static_cast<uint8_t>((_clockInStep + 1) % 6);
            }
            break;
        default: break;
    }
    return result;
}
