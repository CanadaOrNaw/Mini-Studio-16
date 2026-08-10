#include "midi_output.h"

#include "ble_midi.h"
#include "usb_midi.h"

bool midiOutputMessage(const uint8_t* message, uint8_t length) {
    const bool usb = usbMidiSend(message, length);
    const bool ble = bleMidiSend(message, length);
    return usb || ble;
}

void midiOutputNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    const uint8_t message[] = {
        static_cast<uint8_t>(0x90u | (channel & 0x0Fu)),
        static_cast<uint8_t>(note & 0x7Fu),
        static_cast<uint8_t>(velocity & 0x7Fu),
    };
    midiOutputMessage(message, sizeof(message));
}

void midiOutputControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    const uint8_t message[] = {
        static_cast<uint8_t>(0xB0u | (channel & 0x0Fu)),
        static_cast<uint8_t>(control & 0x7Fu),
        static_cast<uint8_t>(value & 0x7Fu),
    };
    midiOutputMessage(message, sizeof(message));
}

void midiOutputRealtime(uint8_t status) {
    if (status == 0xF8 || status == 0xFA || status == 0xFB || status == 0xFC)
        midiOutputMessage(&status, 1);
}

