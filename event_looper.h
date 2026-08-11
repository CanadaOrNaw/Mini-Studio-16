#pragma once

#include "event_looper_core.h"

#include <stdint.h>

extern EventLooperCore g_eventLooper;
extern uint16_t g_eventLoopPosition;

void eventLooperInit();
void eventLooperResetTransport();
void eventLooperSetPosition(uint16_t step);
void eventLooperAdvance();
bool eventLooperRecordSynth(uint16_t step, uint8_t synthTrack,
                            uint8_t midiNote, uint8_t velocity);
bool eventLooperRecordSynthRelease(uint16_t step, uint8_t synthTrack,
                                   uint8_t midiNote);
bool eventLooperRecordDrum(uint16_t step, uint8_t lane, uint8_t velocity);
bool eventLooperRecordSample(uint16_t step, uint8_t slot, uint8_t key,
                             uint8_t velocity);
bool eventLooperRecordControl(uint16_t step, uint8_t control, uint8_t value);
EventLoopRole eventLooperRoleForSynth(uint8_t synthTrack);
const char* eventLooperRoleName(uint8_t track);
