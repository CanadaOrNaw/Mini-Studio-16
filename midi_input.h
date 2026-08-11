#pragma once

#include <stdint.h>

void midiInputInit();
void midiInputFeedByte(uint8_t byte);  // one producer: USB or BLE adapter
void midiInputUpdate();               // main-loop consumer
uint32_t midiInputDroppedEvents();
bool midiInputIsDispatching();
