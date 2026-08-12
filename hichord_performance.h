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
    ARP_UP = 0, ARP_DOWN, ARP_UP_DOWN, ARP_DOWN_UP, ARP_RANDOM,
    ARP_FINGERPICK, ARP_PATTERN_COUNT
};
enum HiChordArpLayer : uint8_t {
    ARP_ONLY = 0, ARP_CHORD_PLUS, ARP_RHYTHM_PLUS, ARP_LAYER_COUNT
};

enum HiChordRate : uint8_t {
    HICHORD_RATE_1_1 = 0, HICHORD_RATE_1_2, HICHORD_RATE_1_4,
    HICHORD_RATE_1_8, HICHORD_RATE_1_16, HICHORD_RATE_1_16T,
    HICHORD_RATE_1_32, HICHORD_RATE_SWING_8, HICHORD_RATE_SWING_16,
    HICHORD_RATE_COUNT
};

enum HiChordStrumSpeed : uint8_t {
    HICHORD_STRUM_SLOW = 0, HICHORD_STRUM_MEDIUM, HICHORD_STRUM_FAST,
    HICHORD_STRUM_SPEED_COUNT
};

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
    uint8_t arpNotes(const ChordVoicing &chord, HiChordArpPattern pattern,
                     uint32_t tick, uint8_t out[2]) const;
    bool setSequenceStep(uint8_t step, const HiChordSequenceStep &value);
    const HiChordSequenceStep &sequenceStep(uint8_t step) const;
private:
    HiChordMode mode_;
    HiChordSequenceStep sequence_[16];
};

uint16_t hiChordStrumSpacingFrames(HiChordStrumSpeed speed,
                                   uint32_t sampleRate);
uint32_t hiChordRateIntervalUs(HiChordRate rate, uint16_t bpm,
                               uint32_t tick);
const char *hiChordRateName(HiChordRate rate);
HiChordRate hiChordAutoDrumRate(ChordDirection direction);
uint32_t hiChordLoopFrames(uint8_t bars, uint16_t bpm, uint32_t sampleRate);
uint32_t hiChordCountInFrames(uint16_t bpm, uint32_t sampleRate);

enum HiChordHiroGrade : uint8_t {
    HICHORD_HIRO_MISS = 0, HICHORD_HIRO_OK, HICHORD_HIRO_GREAT,
    HICHORD_HIRO_PERFECT
};
uint16_t hiChordHiroWindowMs(uint8_t difficulty);
HiChordHiroGrade hiChordHiroGrade(uint8_t expectedDegree, uint8_t playedDegree,
                                  int32_t timingErrorMs, uint8_t difficulty);
const char* hiChordHiroGradeName(HiChordHiroGrade grade);

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
