#pragma once

#include <stdint.h>

// Compact, allocation-free note gate used by chord arps/repeats and the Ear
// Trainer. All scheduled gates are <=350 ms, so signed modular comparison of
// a 16-bit millisecond deadline remains unambiguous across timer wrap.
struct PerformanceGateRelease {
    uint16_t dueMs;
    uint8_t noteAndAudition;
    uint8_t trackRole;
};

static_assert(sizeof(PerformanceGateRelease) == 4,
              "performance gate SRAM layout changed");

inline PerformanceGateRelease performanceGateMake(uint8_t track, uint8_t note,
                                                   uint8_t role, bool audition,
                                                   uint32_t dueUs) {
    PerformanceGateRelease gate = {};
    gate.dueMs = static_cast<uint16_t>(dueUs / 1000u);
    gate.noteAndAudition = static_cast<uint8_t>((note & 0x7Fu) |
        (audition ? 0x80u : 0u));
    gate.trackRole = static_cast<uint8_t>((track & 0x03u) |
        ((role & 0x1Fu) << 2));
    return gate;
}

inline uint8_t performanceGateTrack(const PerformanceGateRelease &gate) {
    return static_cast<uint8_t>(gate.trackRole & 0x03u);
}
inline uint8_t performanceGateRole(const PerformanceGateRelease &gate) {
    return static_cast<uint8_t>(gate.trackRole >> 2);
}
inline uint8_t performanceGateNote(const PerformanceGateRelease &gate) {
    return static_cast<uint8_t>(gate.noteAndAudition & 0x7Fu);
}
inline bool performanceGateAudition(const PerformanceGateRelease &gate) {
    return (gate.noteAndAudition & 0x80u) != 0;
}
inline bool performanceGateDue(const PerformanceGateRelease &gate,
                               uint32_t nowUs) {
    return static_cast<int16_t>(static_cast<uint16_t>(nowUs / 1000u) -
                                gate.dueMs) >= 0;
}
