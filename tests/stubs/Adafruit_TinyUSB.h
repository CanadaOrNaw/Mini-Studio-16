#pragma once

#include <stddef.h>
#include <stdint.h>

class Adafruit_USBD_MIDI {
public:
    bool begin() { return true; }
    int available() const { return 0; }
    int read() { return -1; }
    size_t write(const uint8_t*, size_t length) { return length; }
};

struct TinyUsbDeviceStub {
    bool mounted() const { return true; }
};
static TinyUsbDeviceStub TinyUSBDevice;

