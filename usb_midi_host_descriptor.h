#pragma once

#include <stddef.h>
#include <stdint.h>

struct UsbMidiHostInterface {
    bool found;
    uint8_t interfaceNumber;
    uint8_t alternateSetting;
    uint8_t inputEndpoint;
    uint16_t inputMaxPacket;
    uint8_t outputEndpoint;
    uint16_t outputMaxPacket;
};

UsbMidiHostInterface usbMidiFindStreamingInterface(const uint8_t* descriptor,
                                                   size_t length);
uint8_t usbMidiEventPacketLength(uint8_t codeIndexNumber);
