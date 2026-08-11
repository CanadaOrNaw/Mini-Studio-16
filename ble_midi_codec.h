#pragma once

#include <stddef.h>
#include <stdint.h>

class BleMidiDecoder {
public:
    BleMidiDecoder() : _runningStatus(0) {}
    void reset() { _runningStatus = 0; }

    template <typename Sink>
    bool decode(const uint8_t* packet, size_t length, Sink sink) {
        if (!packet || length < 3 || (packet[0] & 0xC0u) != 0x80u) return false;
        size_t cursor = 1;
        while (cursor < length) {
            uint8_t byte = packet[cursor];
            if (byte & 0x80u) {
                ++cursor;                       // timestamp low byte
                if (cursor >= length) return false;
                byte = packet[cursor++];
            } else if (_runningStatus != 0) {
                // P3 (reconciliation report): BLE-MIDI 1.0 allows running-
                // status data bytes with no interleaved timestamp. Treating
                // them as malformed discarded the rest of the packet after
                // the first message — a stuck-note risk when the matching
                // note-off shared the packet.
                ++cursor;
            } else {
                return false;
            }
            if (byte >= 0xF8u) { sink(byte); continue; }

            uint8_t status = byte;
            uint8_t dataNeeded = 0;
            if (byte & 0x80u) {
                if (byte >= 0x80u && byte <= 0xEFu) {
                    _runningStatus = byte;
                    const uint8_t kind = byte & 0xF0u;
                    dataNeeded = (kind == 0xC0u || kind == 0xD0u) ? 1 : 2;
                } else if (byte == 0xF2u) {
                    _runningStatus = 0;
                    dataNeeded = 2;
                } else {
                    return false;
                }
                sink(byte);
            } else {
                if (_runningStatus == 0) return false;
                status = _runningStatus;
                const uint8_t kind = status & 0xF0u;
                dataNeeded = static_cast<uint8_t>(
                    ((kind == 0xC0u || kind == 0xD0u) ? 1 : 2) - 1);
                sink(byte);
            }

            while (dataNeeded) {
                if (cursor >= length) return false;
                byte = packet[cursor++];
                if (byte >= 0xF8u) { sink(byte); continue; }
                if (byte & 0x80u) return false;
                sink(byte);
                --dataNeeded;
            }
        }
        return true;
    }

private:
    uint8_t _runningStatus;
};

inline size_t bleMidiEncode(uint16_t timestamp, const uint8_t* message,
                            size_t messageLength, uint8_t* output,
                            size_t outputCapacity) {
    if (!message || messageLength == 0 || !output ||
        messageLength + 2 > outputCapacity) return 0;
    timestamp &= 0x1FFFu;
    output[0] = static_cast<uint8_t>(0x80u | ((timestamp >> 7) & 0x3Fu));
    output[1] = static_cast<uint8_t>(0x80u | (timestamp & 0x7Fu));
    for (size_t index = 0; index < messageLength; ++index)
        output[index + 2] = message[index];
    return messageLength + 2;
}

