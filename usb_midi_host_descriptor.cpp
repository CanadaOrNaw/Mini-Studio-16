#include "usb_midi_host_descriptor.h"

namespace {
constexpr uint8_t kDescriptorInterface = 4;
constexpr uint8_t kDescriptorEndpoint = 5;
constexpr uint8_t kClassAudio = 1;
constexpr uint8_t kSubclassMidiStreaming = 3;
constexpr uint8_t kTransferBulk = 2;
}  // namespace

UsbMidiHostInterface usbMidiFindStreamingInterface(const uint8_t* bytes,
                                                   size_t length) {
    UsbMidiHostInterface result = {};
    if (!bytes || length < 2) return result;
    bool midiInterface = false;
    for (size_t offset = 0; offset + 2 <= length;) {
        const uint8_t descriptorLength = bytes[offset];
        const uint8_t descriptorType = bytes[offset + 1];
        if (descriptorLength < 2 || offset + descriptorLength > length) return {};
        if (descriptorType == kDescriptorInterface && descriptorLength >= 9) {
            if (result.found && result.inputEndpoint) return result;
            midiInterface = bytes[offset + 5] == kClassAudio &&
                            bytes[offset + 6] == kSubclassMidiStreaming;
            if (midiInterface) {
                result = {};
                result.found = true;
                result.interfaceNumber = bytes[offset + 2];
                result.alternateSetting = bytes[offset + 3];
            }
        } else if (midiInterface && descriptorType == kDescriptorEndpoint &&
                   descriptorLength >= 7 && (bytes[offset + 3] & 0x03u) == kTransferBulk) {
            const uint8_t endpoint = bytes[offset + 2];
            const uint16_t maximum = static_cast<uint16_t>(bytes[offset + 4]) |
                                     static_cast<uint16_t>(bytes[offset + 5] << 8);
            if (endpoint & 0x80u) {
                result.inputEndpoint = endpoint;
                result.inputMaxPacket = maximum & 0x07FFu;
            } else {
                result.outputEndpoint = endpoint;
                result.outputMaxPacket = maximum & 0x07FFu;
            }
        }
        offset += descriptorLength;
    }
    if (!result.inputEndpoint || !result.inputMaxPacket) result.found = false;
    return result;
}

uint8_t usbMidiEventPacketLength(uint8_t cin) {
    static const uint8_t lengths[16] = {
        0, 0, 2, 3, 3, 1, 2, 3, 3, 3, 3, 3, 2, 2, 3, 1,
    };
    return lengths[cin & 0x0Fu];
}
