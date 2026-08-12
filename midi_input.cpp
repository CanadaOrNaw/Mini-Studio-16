#include "midi_input.h"

#include "midi_event_queue.h"
#include "midi_parser.h"
#include "midi_transport.h"
#include "midi_control_map.h"
#include "event_looper.h"
#include "motion.h"
#include "sequencer.h"

namespace {
MidiParser s_parser;
MidiEventQueue<128> s_queue;
MidiTransportClock s_transport;
bool s_dispatching = false;

void routeEvent(const MidiEvent& event) {
    if (event.type == MIDI_EVENT_CLOCK && s_transport.running())
        sequencerExternalEventTick();
    const MidiTransportResult transport = s_transport.consume(event);
    switch (transport.action) {
        case MIDI_TRANSPORT_START: sequencerExternalStart(true); break;
        case MIDI_TRANSPORT_CONTINUE: sequencerExternalStart(false); break;
        case MIDI_TRANSPORT_STOP: sequencerExternalStop(); break;
        case MIDI_TRANSPORT_STEP: sequencerExternalStep(); break;
        case MIDI_TRANSPORT_SEEK: sequencerExternalSongPosition(transport.songPosition); break;
        default: break;
    }

    if (event.type == MIDI_EVENT_CC) {
        const MidiMappedControl mapped = midiMapControl(event.channel, event.data1);
        if (mapped.kind == MIDI_MAPPED_NONE || mapped.synthTrack >= NUM_SYNTHS) return;
        if (mapped.kind == MIDI_MAPPED_VOLUME) {
            const float normalized = static_cast<float>(event.data2) / 127.0f;
            g_synths[mapped.synthTrack].setVolume(normalized);
            return;
        }
        const uint8_t target = static_cast<uint8_t>(
            (mapped.kind == MIDI_MAPPED_CUTOFF ? MOTION_TARGET_SYNTH1_CUTOFF
                                                : MOTION_TARGET_SYNTH1_RESONANCE) +
            mapped.synthTrack);
        motionApplyRecordedControl(target, event.data2);
        // P3 (reconciliation report): match the motion path's per-step/
        // delta-2 dedupe. Without it, one DAW CC sweep (hundreds of
        // messages) permanently consumed the shared 2,048-event capacity.
        static uint16_t lastStep[MOTION_TARGET_COUNT] = {};
        static uint8_t lastValue[MOTION_TARGET_COUNT] = {};
        static bool seeded[MOTION_TARGET_COUNT] = {};
        const uint16_t step = sequencerEventRecordStep();
        const uint8_t value = event.data2;
        const int16_t delta = static_cast<int16_t>(value) -
                              static_cast<int16_t>(lastValue[target]);
        if (!seeded[target] || lastStep[target] != step ||
            delta >= 2 || delta <= -2) {
            eventLooperRecordControl(step, target, value);
            seeded[target] = true;
            lastStep[target] = step;
            lastValue[target] = value;
        }
        return;
    }

    if (event.type == MIDI_EVENT_NOTE_OFF && event.channel < NUM_SYNTHS) {
        liveSynthRelease(event.channel, event.data1);
        return;
    }
    if (event.type != MIDI_EVENT_NOTE_ON) return;
    if (event.channel < NUM_SYNTHS && event.data1 >= 24 && event.data1 <= 107) {
        const uint8_t note = static_cast<uint8_t>((event.data1 % 12) + 1);
        const uint8_t octave = static_cast<uint8_t>((event.data1 / 12) - 1);
        liveSynthNote(event.channel, note, octave, event.data2 >= 100, false,
                      event.data2);
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
