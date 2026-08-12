#pragma once

#include "chord_engine.h"
#include <stdint.h>

enum HiChordMode : uint8_t {
    HICHORD_PLAY = 0, HICHORD_STRUM, HICHORD_LEAD, HICHORD_DRONE,
    HICHORD_ARPEGGIO, HICHORD_REPEAT, HICHORD_MIC_SAMPLE, HICHORD_DRUM,
    HICHORD_DRUM_LOOPS, HICHORD_AUTO_DRUM, HICHORD_SEQUENCER,
    HICHORD_CHORD_HIRO, HICHORD_EAR_TRAINER, HICHORD_TUNER,
    HICHORD_MIXER, HICHORD_MODE_COUNT
};

enum HiChordArpPattern : uint8_t {
    ARP_UP = 0, ARP_DOWN, ARP_UP_DOWN, ARP_DOWN_UP, ARP_RANDOM, ARP_CHORD, ARP_PATTERN_COUNT
};
enum HiChordArpLayer : uint8_t { ARP_CHORD_ONLY = 0, ARP_BASS_ONLY, ARP_CHORD_AND_BASS };

struct HiChordScheduledNote {
    uint8_t note;
    uint16_t frameOffset;
    uint8_t velocity;
};

struct HiChordSequenceStep {
    uint8_t degree;
    uint8_t direction;
    uint8_t slashDegree;
    uint8_t enabled;
};

class HiChordPerformance {
public:
    HiChordPerformance();
    void reset();
    bool setMode(HiChordMode mode);
    HiChordMode mode() const { return mode_; }
    uint8_t scheduleStrum(const ChordVoicing &chord, bool downward,
                          uint16_t spacingFrames, HiChordScheduledNote out[7]) const;
    uint8_t arpNote(const ChordVoicing &chord, HiChordArpPattern pattern,
                    HiChordArpLayer layer, uint32_t tick) const;
    bool setSequenceStep(uint8_t step, const HiChordSequenceStep &value);
    const HiChordSequenceStep &sequenceStep(uint8_t step) const;
private:
    HiChordMode mode_;
    HiChordSequenceStep sequence_[16];
};

class HiChordDrumGrooves {
public:
    static const uint8_t KIT_COUNT = 7;
    static const uint8_t STYLE_COUNT = 7;
    static const uint8_t VARIATION_COUNT = 8;
    static const uint8_t VOICE_COUNT = 7;
    static bool hit(uint8_t style, uint8_t variation, uint8_t voice, uint8_t step);
};

struct HiChordPracticeSong {
    const char *name;
    uint8_t length;
    uint8_t degrees[16];
};
const HiChordPracticeSong &hiChordPracticeSong(uint8_t index);
uint8_t hiChordPracticeSongCount();

class HiChordEarTrainer {
public:
    explicit HiChordEarTrainer(uint32_t seed = 1) : state_(seed ? seed : 1) {}
    uint8_t nextDegree(uint8_t level);
private:
    uint32_t state_;
};

