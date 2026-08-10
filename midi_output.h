#pragma once

#include <stdint.h>

bool midiOutputMessage(const uint8_t* message, uint8_t length);
void midiOutputNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void midiOutputNoteOff(uint8_t channel, uint8_t note, uint8_t velocity = 0);
void midiOutputControlChange(uint8_t channel, uint8_t control, uint8_t value);
void midiOutputRealtime(uint8_t status);
