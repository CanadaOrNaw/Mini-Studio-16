#include "../midi_event_queue.h"
#include "../midi_parser.h"
#include "../midi_transport.h"

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

    std::cout << "midi: parser, queue and transport tests passed\n";
    return 0;
}
