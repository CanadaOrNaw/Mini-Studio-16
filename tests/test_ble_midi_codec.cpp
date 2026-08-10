#include "../ble_midi_codec.h"

#include <cassert>
#include <iostream>
#include <vector>

int main() {
    BleMidiDecoder decoder;
    std::vector<uint8_t> bytes;
    const uint8_t notes[] = {0x80, 0x81, 0x90, 60, 100,
                             0x82, 61, 110};
    assert(decoder.decode(notes, sizeof(notes),
                          [&](uint8_t byte) { bytes.push_back(byte); }));
    const uint8_t expected[] = {0x90, 60, 100, 61, 110};
    assert(bytes == std::vector<uint8_t>(expected, expected + sizeof(expected)));

    bytes.clear();
    const uint8_t transport[] = {0x80, 0x81, 0xFA,
                                  0x82, 0xF8,
                                  0x83, 0xF2, 0x01, 0x02};
    assert(decoder.decode(transport, sizeof(transport),
                          [&](uint8_t byte) { bytes.push_back(byte); }));
    const uint8_t transportExpected[] = {0xFA, 0xF8, 0xF2, 0x01, 0x02};
    assert(bytes == std::vector<uint8_t>(transportExpected,
                                         transportExpected + sizeof(transportExpected)));

    const uint8_t malformed[] = {0x80, 0x01, 0x90};
    assert(!decoder.decode(malformed, sizeof(malformed), [](uint8_t) {}));

    const uint8_t message[] = {0xB0, 74, 127};
    uint8_t packet[5] = {};
    assert(bleMidiEncode(0x1234, message, 3, packet, sizeof(packet)) == 5);
    assert((packet[0] & 0xC0) == 0x80 && (packet[1] & 0x80));
    assert(packet[2] == 0xB0 && packet[4] == 127);

    std::cout << "ble_midi_codec: framing, running status and transport passed\n";
    return 0;
}
