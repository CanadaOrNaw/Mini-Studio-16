#pragma once

#include <stddef.h>
#include <stdint.h>

struct BleMidiSnapshot {
    bool available;
    bool connected;
    uint32_t packetsReceived;
    uint32_t packetsSent;
    uint32_t malformedPackets;
    uint32_t droppedPackets;
};

void bleMidiInit();
void bleMidiUpdate();
bool bleMidiSend(const uint8_t* message, size_t length);
BleMidiSnapshot bleMidiSnapshot();

