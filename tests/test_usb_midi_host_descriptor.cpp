#include "../usb_midi_host_descriptor.h"

#include <cassert>
#include <iostream>

int main() {
    const uint8_t descriptor[] = {
        9, 2, 48, 0, 2, 1, 0, 0x80, 50,
        9, 4, 0, 0, 0, 1, 1, 0, 0,
        9, 4, 1, 0, 2, 1, 3, 0, 0,
        7, 5, 0x81, 2, 64, 0, 0,
        7, 5, 0x02, 2, 64, 0, 0,
        7, 0x24, 1, 0, 0, 0, 0,
    };
    const UsbMidiHostInterface found =
        usbMidiFindStreamingInterface(descriptor, sizeof(descriptor));
    assert(found.found && found.interfaceNumber == 1 && found.alternateSetting == 0);
    assert(found.inputEndpoint == 0x81 && found.inputMaxPacket == 64);
    assert(found.outputEndpoint == 0x02 && found.outputMaxPacket == 64);

    uint8_t malformed[] = {9, 4, 1};
    assert(!usbMidiFindStreamingInterface(malformed, sizeof(malformed)).found);
    uint8_t notMidi[] = {9, 4, 0, 0, 1, 3, 0, 0, 0, 7, 5, 0x81, 2, 64, 0, 0};
    assert(!usbMidiFindStreamingInterface(notMidi, sizeof(notMidi)).found);

    assert(usbMidiEventPacketLength(0x9) == 3);
    assert(usbMidiEventPacketLength(0xC) == 2);
    assert(usbMidiEventPacketLength(0xF) == 1);
    assert(usbMidiEventPacketLength(0x0) == 0);

    // P3 regression: the MIDI class spec permits interrupt endpoints as
    // well as bulk, and real controllers ship them; both must mount.
    const uint8_t interruptDescriptor[] = {
        9, 2, 32, 0, 1, 1, 0, 0x80, 50,
        9, 4, 0, 0, 2, 1, 3, 0, 0,
        7, 5, 0x82, 3, 32, 0, 4,
        7, 5, 0x03, 3, 32, 0, 4,
    };
    const UsbMidiHostInterface viaInterrupt = usbMidiFindStreamingInterface(
        interruptDescriptor, sizeof(interruptDescriptor));
    assert(viaInterrupt.found && viaInterrupt.inputEndpoint == 0x82);
    assert(viaInterrupt.outputEndpoint == 0x03 && viaInterrupt.inputMaxPacket == 32);
    // Isochronous endpoints are still rejected.
    const uint8_t isoDescriptor[] = {
        9, 4, 0, 0, 1, 1, 3, 0, 0,
        7, 5, 0x81, 1, 64, 0, 0,
    };
    assert(usbMidiFindStreamingInterface(
        isoDescriptor, sizeof(isoDescriptor)).inputEndpoint == 0);

    std::cout << "usb_midi_host_descriptor: interface and packet decoding passed\n";
    return 0;
}
