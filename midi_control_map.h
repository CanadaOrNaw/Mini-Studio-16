#pragma once

#include <stdint.h>

enum MidiMappedControlKind : uint8_t {
    MIDI_MAPPED_NONE = 0,
    MIDI_MAPPED_CUTOFF,
    MIDI_MAPPED_RESONANCE,
    MIDI_MAPPED_VOLUME,
};

struct MidiMappedControl {
    MidiMappedControlKind kind;
    uint8_t synthTrack;
};

// Channel-local conventional controls plus six channel-independent knobs:
//   CC74 cutoff, CC71 resonance, CC7 volume on channels 1..3
//   CC20..22 cutoff and CC23..25 resonance for synths 1..3 on any channel
inline MidiMappedControl midiMapControl(uint8_t channel, uint8_t control) {
    if (control >= 20 && control <= 22)
        return {MIDI_MAPPED_CUTOFF, static_cast<uint8_t>(control - 20)};
    if (control >= 23 && control <= 25)
        return {MIDI_MAPPED_RESONANCE, static_cast<uint8_t>(control - 23)};
    if (channel < 3) {
        if (control == 74) return {MIDI_MAPPED_CUTOFF, channel};
        if (control == 71) return {MIDI_MAPPED_RESONANCE, channel};
        if (control == 7) return {MIDI_MAPPED_VOLUME, channel};
    }
    return {MIDI_MAPPED_NONE, 0};
}
