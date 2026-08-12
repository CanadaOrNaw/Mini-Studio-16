#pragma once

#include <stdint.h>

void inputInit();
void inputUpdate();

// Stops latched/pulsed HiChord notes, including legacy MG/303 voices whose
// ordinary note-off behavior is intentionally unchanged.
void inputStopHiChordPerformanceNotes();
uint16_t inputChordHiroScore();
