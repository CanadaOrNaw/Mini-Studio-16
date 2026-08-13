#include "hichord_performance.h"
#include <string.h>

namespace {
// A2-P2 (alpha.2 reconciliation): `(tick * 1103515245 + 12345) % count` is a
// single LCG step, so consecutive ticks stay correlated and the "random"
// arp degenerated to a period-4 sequence. A 32-bit integer finalizer
// (Murmur3's) decorrelates adjacent ticks while remaining a pure function
// of the tick, so playback stays deterministic and reproducible.
uint32_t scrambleTick(uint32_t tick) {
    uint32_t h = tick + 0x9E3779B9u;
    h ^= h >> 16; h *= 0x85EBCA6Bu;
    h ^= h >> 13; h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h;
}
}  // namespace

HiChordPerformance::HiChordPerformance() { reset(); }
void HiChordPerformance::reset() {
    mode_ = HICHORD_PLAY; memset(sequence_, 0, sizeof(sequence_));
    for (uint8_t i = 0; i < 16; ++i) sequence_[i].slashDegree = 0xFF;
}
bool HiChordPerformance::setMode(HiChordMode mode) {
    if (mode >= HICHORD_MODE_COUNT) return false;
    mode_ = mode; return true;
}
uint8_t HiChordPerformance::scheduleStrum(const ChordVoicing &chord, bool downward,
                                          uint16_t spacing, HiChordScheduledNote out[7]) const {
    if (!out) return 0;
    for (uint8_t i = 0; i < chord.count; ++i) {
        const uint8_t source = downward ? static_cast<uint8_t>(chord.count - 1 - i) : i;
        out[i].note = chord.notes[source];
        out[i].frameOffset = static_cast<uint16_t>(i * spacing);
        out[i].velocity = static_cast<uint8_t>(110 - i * 3);
    }
    return chord.count;
}
uint8_t HiChordPerformance::arpNotes(const ChordVoicing &chord,
                                     HiChordArpPattern pattern,
                                     uint32_t tick, uint8_t out[2]) const {
    if (!out || chord.chordToneCount == 0 || pattern >= ARP_PATTERN_COUNT)
        return 0;
    const uint8_t count = chord.chordToneCount;
    if (pattern == ARP_FINGERPICK) {
        static const uint8_t pairs[4][2] = {{0,2},{1,0},{0,1},{2,1}};
        const uint8_t pair = static_cast<uint8_t>(tick & 3u);
        out[0] = chord.notes[pairs[pair][0] % count];
        out[1] = chord.notes[pairs[pair][1] % count];
        return count > 1 ? 2 : 1;
    }
    uint32_t index = tick % count;
    switch (pattern) {
        case ARP_DOWN: index = count - 1 - index; break;
        case ARP_UP_DOWN: {
            const uint8_t span = count > 1 ? static_cast<uint8_t>(count * 2 - 2) : 1;
            index = tick % span; if (index >= count) index = span - index; break;
        }
        case ARP_DOWN_UP: {
            const uint8_t span = count > 1 ? static_cast<uint8_t>(count * 2 - 2) : 1;
            index = tick % span; index = index < count ? count - 1 - index : index - count + 1; break;
        }
        case ARP_RANDOM: index = scrambleTick(tick) % count; break;
        case ARP_FINGERPICK: break;
        case ARP_UP: default: break;
    }
    out[0] = chord.notes[index];
    return 1;
}

uint16_t hiChordStrumSpacingFrames(HiChordStrumSpeed speed,
                                   uint32_t sampleRate) {
    static const uint16_t milliseconds[HICHORD_STRUM_SPEED_COUNT] = {120, 80, 40};
    if (speed >= HICHORD_STRUM_SPEED_COUNT || sampleRate == 0) return 0;
    const uint32_t frames = sampleRate * milliseconds[speed] / 1000u;
    return static_cast<uint16_t>(frames > 65535u ? 65535u : frames);
}

uint32_t hiChordRateIntervalUs(HiChordRate rate, uint16_t bpm,
                               uint32_t tick) {
    if (rate >= HICHORD_RATE_COUNT || bpm == 0) return 0;
    const uint32_t beat = 60000000UL / bpm;
    switch (rate) {
        case HICHORD_RATE_1_1: return beat * 4u;
        case HICHORD_RATE_1_2: return beat * 2u;
        case HICHORD_RATE_1_4: return beat;
        case HICHORD_RATE_1_8: return beat / 2u;
        case HICHORD_RATE_1_16: return beat / 4u;
        case HICHORD_RATE_1_16T: return beat / 6u;
        case HICHORD_RATE_1_32: return beat / 8u;
        case HICHORD_RATE_SWING_8:
            return tick & 1u ? beat / 3u : beat * 2u / 3u;
        case HICHORD_RATE_SWING_16:
            return tick & 1u ? beat / 6u : beat / 3u;
        default: return 0;
    }
}

const char *hiChordRateName(HiChordRate rate) {
    static const char *names[HICHORD_RATE_COUNT] = {
        "1/1", "1/2", "1/4", "1/8", "1/16", "1/16T", "1/32", "SW8", "SW16"
    };
    return rate < HICHORD_RATE_COUNT ? names[rate] : "?";
}

HiChordRate hiChordAutoDrumRate(ChordDirection direction) {
    switch (direction) {
        case CHORD_DIR_N: return HICHORD_RATE_1_4;
        case CHORD_DIR_E: return HICHORD_RATE_1_8;
        case CHORD_DIR_S: return HICHORD_RATE_1_16;
        case CHORD_DIR_W: return HICHORD_RATE_1_32;
        case CHORD_DIR_NE: return HICHORD_RATE_SWING_8;
        case CHORD_DIR_SE: return HICHORD_RATE_SWING_16;
        case CHORD_DIR_SW: return HICHORD_RATE_1_16T;
        case CHORD_DIR_NW: return HICHORD_RATE_SWING_8;
        case CHORD_DIR_CENTER:
        default: return HICHORD_RATE_1_8;
    }
}

uint32_t hiChordLoopFrames(uint8_t bars, uint16_t bpm, uint32_t sampleRate) {
    if (bars == 0 || bars > 8 || bpm == 0 || sampleRate == 0) return 0;
    const uint64_t frames = static_cast<uint64_t>(bars) * 4u * 60u * sampleRate / bpm;
    return frames > 0xFFFFFFFFu ? 0xFFFFFFFFu : static_cast<uint32_t>(frames);
}

uint32_t hiChordCountInFrames(uint16_t bpm, uint32_t sampleRate) {
    if (bpm == 0 || sampleRate == 0) return 0;
    return static_cast<uint32_t>(static_cast<uint64_t>(4u) * 60u * sampleRate / bpm);
}

uint16_t hiChordHiroWindowMs(uint8_t difficulty) {
    static const uint16_t windows[4] = {200, 150, 100, 50};
    return windows[difficulty < 4 ? difficulty : 3];
}

HiChordHiroGrade hiChordHiroGrade(uint8_t expectedDegree, uint8_t playedDegree,
                                  int32_t timingErrorMs, uint8_t difficulty) {
    if (expectedDegree >= 7 || playedDegree != expectedDegree)
        return HICHORD_HIRO_MISS;
    const uint32_t error = timingErrorMs < 0
        ? static_cast<uint32_t>(-static_cast<int64_t>(timingErrorMs))
        : static_cast<uint32_t>(timingErrorMs);
    const uint16_t window = hiChordHiroWindowMs(difficulty);
    if (error > window) return HICHORD_HIRO_MISS;
    if (error <= window / 4u) return HICHORD_HIRO_PERFECT;
    if (error <= window / 2u) return HICHORD_HIRO_GREAT;
    return HICHORD_HIRO_OK;
}

const char* hiChordHiroGradeName(HiChordHiroGrade grade) {
    static const char* names[] = {"MISS", "OK", "GREAT", "PERFECT"};
    return grade <= HICHORD_HIRO_PERFECT ? names[grade] : "MISS";
}
bool HiChordPerformance::setSequenceStep(uint8_t step, const HiChordSequenceStep &value) {
    if (step >= 16 || value.degree >= 7 || value.direction > CHORD_DIR_NW ||
        (value.slashDegree != 0xFF && value.slashDegree >= 7)) return false;
    sequence_[step] = value; sequence_[step].enabled = value.enabled ? 1 : 0; return true;
}
const HiChordSequenceStep &HiChordPerformance::sequenceStep(uint8_t step) const {
    return sequence_[step < 16 ? step : 0];
}

bool HiChordDrumGrooves::hit(uint8_t style, uint8_t variation, uint8_t voice, uint8_t step) {
    if (style >= STYLE_COUNT || variation >= VARIATION_COUNT || voice >= VOICE_COUNT || step >= 16)
        return false;
    static const uint16_t base[STYLE_COUNT][VOICE_COUNT] = {
        {0x1111,0x4040,0x5555,0x0004,0x0000,0x0000,0x0000},
        {0x1011,0x4040,0x5555,0x0800,0x0000,0x0000,0x0000},
        {0x1101,0x4040,0x7777,0x0020,0x0000,0x0000,0x0000},
        {0x0101,0x4444,0x5555,0x2200,0x0000,0x0000,0x0000},
        {0x1111,0x0404,0x3333,0x8000,0x0000,0x0000,0x0000},
        {0x1010,0x4040,0xFFFF,0x0080,0x0000,0x0000,0x0000},
        {0x1111,0x4040,0x5555,0x0000,0x0100,0x0000,0x0000}
    };
    uint16_t mask = base[style][voice];
    // Eight deterministic variations add/remove tasteful offbeats while
    // keeping every style's fundamental kick/snare identity intact.
    const uint16_t spice = static_cast<uint16_t>(1u << ((style * 3u + voice * 5u + variation * 2u) & 15u));
    if (variation & 1u) mask |= spice;
    if (variation & 2u) mask |= static_cast<uint16_t>(spice << 1 | spice >> 15);
    if ((variation & 4u) && voice > 1) mask ^= static_cast<uint16_t>(spice << 2 | spice >> 14);
    return (mask & static_cast<uint16_t>(1u << step)) != 0;
}

static const HiChordPracticeSong kSongs[16] = {
    {"FOUR CHORDS",4,{0,4,5,3}}, {"DOO WOP",4,{0,5,3,4}},
    {"BLUES I",12,{0,0,0,0,3,3,0,0,4,3,0,4}}, {"FIFTIES",4,{0,5,3,4}},
    {"POP FALL",4,{5,3,0,4}}, {"AXIS",4,{0,4,5,3}},
    {"MINOR WALK",4,{5,3,4,0}}, {"MIXO JAM",4,{0,6,3,0}},
    {"JAZZ TURN",4,{1,4,0,5}}, {"CADENCE",4,{0,3,4,0}},
    {"DORIAN",4,{0,1,3,4}}, {"LYDIAN",4,{0,1,4,3}},
    {"PACHELBEL",8,{0,4,5,2,3,0,3,4}}, {"DESCENT",7,{6,5,4,3,2,1,0}},
    {"ASCENT",7,{0,1,2,3,4,5,6}}, {"RANDOM LAB",8,{0,2,5,1,4,6,3,0}}
};
const HiChordPracticeSong &hiChordPracticeSong(uint8_t index) { return kSongs[index < 16 ? index : 0]; }
uint8_t hiChordPracticeSongCount() { return 16; }

uint8_t HiChordEarTrainer::nextDegree(uint8_t level) {
    if (level > 3) level = 3;
    state_ = state_ * 1664525u + 1013904223u;
    static const uint8_t choices[4] = {2, 3, 5, 7};
    return static_cast<uint8_t>((state_ >> 16) % choices[level]);
}
