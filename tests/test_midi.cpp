#include "../midi_event_queue.h"
#include "../midi_parser.h"
#include "../midi_transport.h"
#include "../midi_clock_output.h"
#include "../midi_control_map.h"

#include <cassert>
#include <iostream>

static MidiEvent feedUntilEvent(MidiParser& parser, const uint8_t* bytes, size_t count) {
    MidiEvent event = {};
    bool emitted = false;
    for (size_t index = 0; index < count; ++index)
        if (parser.feed(bytes[index], event)) emitted = true;
    assert(emitted);
    return event;
}

int main() {
    MidiParser parser;
    const uint8_t note[] = {0x90, 60, 100};
    MidiEvent event = feedUntilEvent(parser, note, sizeof(note));
    assert(event.type == MIDI_EVENT_NOTE_ON && event.channel == 0);
    assert(event.data1 == 60 && event.data2 == 100);

    // Running status plus realtime clock interleaved between data bytes.
    MidiEvent intermediate = {};
    assert(!parser.feed(64, intermediate));
    assert(parser.feed(0xF8, intermediate) && intermediate.type == MIDI_EVENT_CLOCK);
    assert(parser.feed(0, intermediate) && intermediate.type == MIDI_EVENT_NOTE_OFF);
    assert(intermediate.data1 == 64);

    const uint8_t cc[] = {0xB2, 74, 99};
    event = feedUntilEvent(parser, cc, sizeof(cc));
    assert(event.type == MIDI_EVENT_CC && event.channel == 2);
    assert(event.data1 == 74 && event.data2 == 99);

    // Realtime remains visible inside SysEx while SysEx data itself is ignored.
    assert(!parser.feed(0xF0, intermediate));
    assert(!parser.feed(0x01, intermediate));
    assert(parser.feed(0xFA, intermediate) && intermediate.type == MIDI_EVENT_START);
    assert(!parser.feed(0x02, intermediate));
    assert(!parser.feed(0xF7, intermediate));

    const uint8_t spp[] = {0xF2, 0x01, 0x02};
    event = feedUntilEvent(parser, spp, sizeof(spp));
    assert(event.type == MIDI_EVENT_SONG_POSITION && event.value14 == 257);

    MidiTransportClock transport;
    event.type = MIDI_EVENT_START;
    assert(transport.consume(event).action == MIDI_TRANSPORT_START);
    event.type = MIDI_EVENT_CLOCK;
    assert(transport.consume(event).action == MIDI_TRANSPORT_STEP);
    for (int tick = 1; tick < 6; ++tick)
        assert(transport.consume(event).action == MIDI_TRANSPORT_NONE);
    assert(transport.consume(event).action == MIDI_TRANSPORT_STEP);
    event.type = MIDI_EVENT_STOP;
    assert(transport.consume(event).action == MIDI_TRANSPORT_STOP);
    event.type = MIDI_EVENT_CLOCK;
    assert(transport.consume(event).action == MIDI_TRANSPORT_NONE);

    MidiEventQueue<4> queue;
    event.type = MIDI_EVENT_NOTE_ON;
    for (int i = 0; i < 4; ++i) assert(queue.push(event));
    assert(!queue.push(event) && queue.dropped() == 1);
    for (int i = 0; i < 4; ++i) assert(queue.pop(event));
    assert(queue.size() == 0);

    MidiClockOutputScheduler outputClock;
    outputClock.reset();
    outputClock.start(1000);
    const uint32_t clockPeriod = 60000000u / 128u / 24u;
    assert(outputClock.pulsesDue(1000 + clockPeriod - 1, 128) == 0);
    assert(outputClock.pulsesDue(1000 + clockPeriod, 128) == 1);
    assert(outputClock.pulsesDue(1000 + 6 * clockPeriod, 128) == 5);
    // A long stall is bounded and counted instead of generating an unlimited burst.
    assert(outputClock.pulsesDue(1000 + 30 * clockPeriod, 128) == 6);
    assert(outputClock.dropped() == 18);
    outputClock.stop();
    assert(outputClock.pulsesDue(1000 + 40 * clockPeriod, 128) == 0);

    MidiClockOutputScheduler wrappedClock;
    wrappedClock.reset();
    wrappedClock.start(0xFFFFFF00u);
    assert(wrappedClock.pulsesDue(0xFFFFFF00u + 25000u, 100) == 1);

    MidiMappedControl mapped = midiMapControl(2, 74);
    assert(mapped.kind == MIDI_MAPPED_CUTOFF && mapped.synthTrack == 2);
    mapped = midiMapControl(0, 71);
    assert(mapped.kind == MIDI_MAPPED_RESONANCE && mapped.synthTrack == 0);
    mapped = midiMapControl(1, 7);
    assert(mapped.kind == MIDI_MAPPED_VOLUME && mapped.synthTrack == 1);
    mapped = midiMapControl(15, 21);
    assert(mapped.kind == MIDI_MAPPED_CUTOFF && mapped.synthTrack == 1);
    mapped = midiMapControl(15, 24);
    assert(mapped.kind == MIDI_MAPPED_RESONANCE && mapped.synthTrack == 1);
    assert(midiMapControl(15, 74).kind == MIDI_MAPPED_NONE);

    // P2-1 regression: ALL system-realtime bytes (0xF8-0xFF) are fully
    // transparent. Active Sensing (0xFE) interleaved inside a note-on used
    // to abort the message and cancel running status, dropping notes.
    {
        MidiParser transparent;
        transparent.reset();
        MidiEvent event = {};
        assert(!transparent.feed(0x90, event));
        assert(!transparent.feed(0x3C, event));
        assert(!transparent.feed(0xFE, event));      // interleaved sensing
        assert(transparent.feed(0x64, event));       // note completes anyway
        assert(event.type == MIDI_EVENT_NOTE_ON && event.data1 == 0x3C);
        // Running status survives 0xFE/0xFF/0xF9/0xFD between messages.
        assert(!transparent.feed(0xFE, event));
        assert(!transparent.feed(0xFF, event));
        assert(!transparent.feed(0xF9, event));
        assert(!transparent.feed(0xFD, event));
        assert(!transparent.feed(0x40, event));      // running-status data
        assert(transparent.feed(0x50, event));
        assert(event.type == MIDI_EVENT_NOTE_ON && event.data1 == 0x40);
    }

    std::cout << "midi: parser, queue, transport, CC map and output clock passed\n";
    return 0;
}
