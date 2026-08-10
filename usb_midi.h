#pragma once

#include <stddef.h>
#include <stdint.h>

struct UsbMidiSnapshot {
    bool available;
    bool mounted;
    uint32_t bytesReceived;
    uint32_t messagesSent;
    uint32_t sendErrors;
};

void usbMidiInit();
void usbMidiUpdate();
bool usbMidiSend(const uint8_t* message, size_t length);
UsbMidiSnapshot usbMidiSnapshot();

