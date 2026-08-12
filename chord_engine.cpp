#include "chord_engine.h"

#include <string.h>

namespace {
static const int8_t kScales[CHORD_SCALE_COUNT][7] = {
    {0,2,4,5,7,9,11}, {0,2,3,5,7,8,10}, {0,2,3,5,7,8,11},
    {0,2,3,5,7,9,11}, {0,2,4,7,9,12,14}, {0,3,5,7,10,12,15},
    {0,3,5,6,7,10,12}, {0,2,3,5,7,9,10}, {0,2,4,5,7,9,10},
    {0,2,4,6,7,9,11}
};

struct Formula { uint8_t count; int8_t note[6]; };
static const Formula kFormula[CHORD_TYPE_COUNT] = {
    {3,{0,4,7,0,0,0}}, {3,{0,3,7,0,0,0}}, {3,{0,3,6,0,0,0}},
    {3,{0,4,6,0,0,0}}, {3,{0,4,8,0,0,0}}, {3,{0,5,7,0,0,0}},
    {3,{0,2,7,0,0,0}}, {4,{0,4,7,11,0,0}}, {4,{0,3,7,10,0,0}},
    {4,{0,4,7,10,0,0}}, {4,{0,4,7,9,0,0}}, {4,{0,3,7,9,0,0}},
    {5,{0,4,7,11,14,0}}, {5,{0,3,7,10,14,0}}, {5,{0,4,7,10,15,0}},
    {4,{0,3,6,10,0,0}}, {5,{0,4,7,10,14,0}}, {4,{0,4,7,14,0,0}},
    {4,{0,4,7,17,0,0}}, {6,{0,3,7,10,14,17}}, {4,{0,5,7,10,0,0}},
    {4,{0,3,7,11,0,0}}, {6,{0,4,7,11,14,21}}, {5,{0,4,7,9,14,0}},
    {5,{0,4,7,11,18,0}}, {6,{0,4,7,10,14,21}}, {5,{0,4,7,10,13,0}},
    {6,{0,4,6,10,13,15}}
};

static const char *kScaleNames[CHORD_SCALE_COUNT] = {
    "MAJOR", "NAT MIN", "HARM MIN", "MELO MIN", "MAJ PENT",
    "MIN PENT", "BLUES", "DORIAN", "MIXOLYD", "LYDIAN"
};
static const char *kTypeNames[CHORD_TYPE_COUNT] = {
    "MAJ","MIN","DIM","b5","AUG","SUS4","SUS2","MAJ7","MIN7","7",
    "MAJ6","MIN6","MAJ9","MIN9","7#9","m7b5","9","ADD9","ADD11",
    "MIN11","SUS4+7","mMAJ7","MAJ13","6/9","MAJ7#11","13","7b9","7ALT"
};

static uint8_t clampMidi(int value) {
    if (value < 0) return 0;
    if (value > 127) return 127;
    return static_cast<uint8_t>(value);
}

static ChordType diatonicType(ChordScale scale, uint8_t degree) {
    const int root = kScales[scale][degree];
    int third = kScales[scale][(degree + 2) % 7];
    int fifth = kScales[scale][(degree + 4) % 7];
    if (degree + 2 >= 7) third += 12;
    if (degree + 4 >= 7) fifth += 12;
    third -= root;
    fifth -= root;
    if (third == 3 && fifth == 6) return CHORD_DIMINISHED;
    if (third == 4 && fifth == 8) return CHORD_AUGMENTED;
    return third <= 3 ? CHORD_MINOR : CHORD_MAJOR;
}

static int absInt(int value) { return value < 0 ? -value : value; }
}

ChordEngine::ChordEngine() { reset(); }
void ChordEngine::reset() { previousCount_ = 0; memset(previous_, 0, sizeof(previous_)); }

ChordSettings ChordEngine::defaults() {
    ChordSettings s;
    s.key = 0; s.scale = SCALE_MAJOR; s.map = CHORD_MAP_DEFAULT; s.octave = 4;
    s.bassMode = CHORD_BASS_ROOT; s.voiceLeading = true;
    for (uint8_t i = 0; i < 7; ++i) {
        s.octaveShift[i] = 0; s.inversion[i] = 0; s.lockedType[i] = CHORD_TYPE_COUNT;
    }
    return s;
}

uint8_t ChordEngine::intervals(ChordType type, int8_t out[6]) {
    if (type >= CHORD_TYPE_COUNT) type = CHORD_MAJOR;
    memcpy(out, kFormula[type].note, sizeof(kFormula[type].note));
    return kFormula[type].count;
}

ChordType ChordEngine::modifiedType(ChordType base, ChordMap map, ChordDirection d) {
    if (d == CHORD_DIR_CENTER) return base;
    const bool minor = base == CHORD_MINOR || base == CHORD_DIMINISHED;
    static const ChordType def[8] = { CHORD_MINOR, CHORD_DOM7, CHORD_MAJ7, CHORD_MAJ9,
        CHORD_SUS4, CHORD_MAJ6, CHORD_DIMINISHED, CHORD_AUGMENTED };
    static const ChordType ext[8] = { CHORD_MIN6, CHORD_DOM9, CHORD_ADD11, CHORD_MIN11,
        CHORD_DOM7_SHARP9, CHORD_ADD9, CHORD_SUS4_7, CHORD_HALF_DIM7 };
    static const ChordType chr[8] = { CHORD_MIN_MAJ7, CHORD_DOM13, CHORD_6_9, CHORD_DOM7_ALT,
        CHORD_MAJ13, CHORD_DOM7_FLAT9, CHORD_FLAT5, CHORD_MAJ7_SHARP11 };
    const uint8_t index = static_cast<uint8_t>(d) - 1;
    if (map == CHORD_MAP_EXTENDED) return ext[index];
    if (map == CHORD_MAP_CHROMATIC) return chr[index];
    if (d == CHORD_DIR_N) return minor ? CHORD_MAJOR : CHORD_MINOR;
    if (d == CHORD_DIR_E) return minor ? CHORD_MIN7 : CHORD_MAJ7;
    if (d == CHORD_DIR_SE) return minor ? CHORD_MIN9 : CHORD_MAJ9;
    if (d == CHORD_DIR_SW) return minor ? CHORD_SUS2 : CHORD_MAJ6;
    return def[index];
}

ChordVoicing ChordEngine::build(const ChordSettings &raw, uint8_t degree,
                                ChordDirection direction, uint8_t slashDegree) {
    ChordSettings s = raw;
    if (s.key > 11) s.key %= 12;
    if (s.scale >= CHORD_SCALE_COUNT) s.scale = SCALE_MAJOR;
    if (s.map > CHORD_MAP_CHROMATIC) s.map = CHORD_MAP_DEFAULT;
    degree %= 7;
    const int rootPc = (s.key + kScales[s.scale][degree]) % 12;
    ChordType type = diatonicType(s.scale, degree);
    if (s.lockedType[degree] < CHORD_TYPE_COUNT) type = static_cast<ChordType>(s.lockedType[degree]);
    type = modifiedType(type, s.map, direction);

    int8_t formula[6];
    const uint8_t toneCount = intervals(type, formula);
    int root = 12 * (static_cast<int>(s.octave) + 1) + rootPc;
    int shift = s.octaveShift[degree];
    if (shift < -2) shift = -2; else if (shift > 2) shift = 2;
    root += shift * 12;

    int notes[6];
    for (uint8_t i = 0; i < toneCount; ++i) notes[i] = root + formula[i];
    int inversion = s.inversion[degree];
    if (inversion < -2) inversion = -2;
    if (inversion > static_cast<int>(toneCount) - 1) inversion = toneCount - 1;
    if (inversion > 0) for (int n = 0; n < inversion; ++n) notes[n] += 12;
    if (inversion < 0) for (int n = toneCount + inversion; n < toneCount; ++n) notes[n] -= 12;

    if (s.voiceLeading && previousCount_ > 0) {
        int bestShift = 0;
        int bestScore = 0x7fffffff;
        for (int octave = -1; octave <= 1; ++octave) {
            int score = 0;
            const uint8_t compare = toneCount < previousCount_ ? toneCount : previousCount_;
            for (uint8_t i = 0; i < compare; ++i) score += absInt(notes[i] + octave * 12 - previous_[i]);
            if (score < bestScore) { bestScore = score; bestShift = octave * 12; }
        }
        for (uint8_t i = 0; i < toneCount; ++i) notes[i] += bestShift;
    }

    ChordVoicing out = {};
    out.root = static_cast<uint8_t>(rootPc); out.type = type; out.chordToneCount = toneCount;
    for (uint8_t i = 0; i < toneCount; ++i) {
        out.notes[out.count++] = clampMidi(notes[i]);
        previous_[i] = out.notes[i];
    }
    previousCount_ = toneCount;
    out.bass = 0xFF;
    if (s.bassMode != CHORD_BASS_OFF && out.count < 7) {
        uint8_t bassDegree = degree;
        if (s.bassMode == CHORD_BASS_SLASH && slashDegree < 7) bassDegree = slashDegree;
        int bassPc = (s.key + kScales[s.scale][bassDegree]) % 12;
        out.bass = clampMidi(12 * (static_cast<int>(s.octave) - 1) + bassPc);
        out.notes[out.count++] = out.bass;
    }
    return out;
}

const char *ChordEngine::scaleName(ChordScale scale) {
    return scale < CHORD_SCALE_COUNT ? kScaleNames[scale] : "MAJOR";
}
const char *ChordEngine::typeName(ChordType type) {
    return type < CHORD_TYPE_COUNT ? kTypeNames[type] : "MAJ";
}

