#include "usb_midi.h"

#include "midi_input.h"

#include <Adafruit_TinyUSB.h>

namespace {
Adafruit_USBD_MIDI s_usbMidi;
UsbMidiSnapshot s_snapshot = {};
}  // namespace

void usbMidiInit() {
    s_snapshot = {};
    s_snapshot.available = s_usbMidi.begin();
}

void usbMidiUpdate() {
    if (!s_snapshot.available) return;
    s_snapshot.mounted = TinyUSBDevice.mounted();
    uint8_t budget = 64;
    while (budget-- && s_usbMidi.available() > 0) {
        const int value = s_usbMidi.read();
        if (value < 0) break;
        midiInputFeedByte(static_cast<uint8_t>(value));
        ++s_snapshot.bytesReceived;
    }
}

bool usbMidiSend(const uint8_t* message, size_t length) {
    if (!s_snapshot.available || !message || length == 0 || length > 3) return false;
    const bool ok = s_usbMidi.write(message, length) == length;
    if (ok) ++s_snapshot.messagesSent;
    else ++s_snapshot.sendErrors;
    return ok;
}

UsbMidiSnapshot usbMidiSnapshot() { return s_snapshot; }

