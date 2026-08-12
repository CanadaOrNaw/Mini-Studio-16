#include "hichord_performance.h"
#include <string.h>

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
uint8_t HiChordPerformance::arpNote(const ChordVoicing &chord, HiChordArpPattern pattern,
                                    HiChordArpLayer layer, uint32_t tick) const {
    if (chord.count == 0 || pattern >= ARP_PATTERN_COUNT || layer > ARP_CHORD_AND_BASS) return 0xFF;
    const uint8_t tones = chord.chordToneCount;
    if (layer == ARP_BASS_ONLY) return chord.bass;
    const uint8_t count = layer == ARP_CHORD_ONLY || chord.bass == 0xFF ? tones : chord.count;
    if (!count) return 0xFF;
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
        case ARP_RANDOM: index = (tick * 1103515245u + 12345u) % count; break;
        case ARP_CHORD: index = tick % count; break;
        case ARP_UP: default: break;
    }
    return chord.notes[index];
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

