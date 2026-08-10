#include "midi_input.h"

#include "midi_event_queue.h"
#include "midi_parser.h"
#include "midi_transport.h"
#include "sequencer.h"

namespace {
MidiParser s_parser;
MidiEventQueue<128> s_queue;
MidiTransportClock s_transport;
bool s_dispatching = false;

void routeEvent(const MidiEvent& event) {
    const MidiTransportResult transport = s_transport.consume(event);
    switch (transport.action) {
        case MIDI_TRANSPORT_START: sequencerExternalStart(true); break;
        case MIDI_TRANSPORT_CONTINUE: sequencerExternalStart(false); break;
        case MIDI_TRANSPORT_STOP: sequencerExternalStop(); break;
        case MIDI_TRANSPORT_STEP: sequencerExternalStep(); break;
        case MIDI_TRANSPORT_SEEK: sequencerExternalSongPosition(transport.songPosition); break;
        default: break;
    }

    if (event.type != MIDI_EVENT_NOTE_ON) return;
    if (event.channel < NUM_SYNTHS && event.data1 >= 24 && event.data1 <= 107) {
        const uint8_t note = static_cast<uint8_t>((event.data1 % 12) + 1);
        const uint8_t octave = static_cast<uint8_t>((event.data1 / 12) - 1);
        liveSynthNote(event.channel, note, octave, event.data2 >= 100, false);
    } else if (event.channel == 9 && event.data1 >= 36 && event.data1 < 36 + NUM_DRUM_LANES) {
        liveDrumHit(static_cast<uint8_t>(event.data1 - 36));
    }
}
}  // namespace

void midiInputInit() {
    s_parser.reset();
    s_queue.reset();
    s_transport = MidiTransportClock();
}

void midiInputFeedByte(uint8_t byte) {
    MidiEvent event = {};
    if (s_parser.feed(byte, event)) s_queue.push(event);
}

void midiInputUpdate() {
    MidiEvent event = {};
    uint8_t budget = 32;
    while (budget-- && s_queue.pop(event)) {
        s_dispatching = true;
        routeEvent(event);
        s_dispatching = false;
    }
}

uint32_t midiInputDroppedEvents() { return s_queue.dropped(); }
bool midiInputIsDispatching() { return s_dispatching; }
