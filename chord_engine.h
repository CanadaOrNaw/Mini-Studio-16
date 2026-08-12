#pragma once

#include <stdint.h>

// Hardware-independent HiChord-compatible harmony core. All state is fixed-size
// so chord generation is safe to call from the real-time control path.
enum ChordScale : uint8_t {
    SCALE_MAJOR = 0, SCALE_NATURAL_MINOR, SCALE_HARMONIC_MINOR,
    SCALE_MELODIC_MINOR, SCALE_MAJOR_PENTATONIC, SCALE_MINOR_PENTATONIC,
    SCALE_BLUES, SCALE_DORIAN, SCALE_MIXOLYDIAN, SCALE_LYDIAN,
    CHORD_SCALE_COUNT
};

enum ChordType : uint8_t {
    CHORD_MAJOR = 0, CHORD_MINOR, CHORD_DIMINISHED, CHORD_FLAT5,
    CHORD_AUGMENTED, CHORD_SUS4, CHORD_SUS2, CHORD_MAJ7, CHORD_MIN7,
    CHORD_DOM7, CHORD_MAJ6, CHORD_MIN6, CHORD_MAJ9, CHORD_MIN9,
    CHORD_DOM7_SHARP9, CHORD_HALF_DIM7, CHORD_DOM9, CHORD_ADD9,
    CHORD_ADD11, CHORD_MIN11, CHORD_SUS4_7, CHORD_MIN_MAJ7,
    CHORD_MAJ13, CHORD_6_9, CHORD_MAJ7_SHARP11, CHORD_DOM13,
    CHORD_DOM7_FLAT9, CHORD_DOM7_ALT, CHORD_TYPE_COUNT
};

enum ChordMap : uint8_t { CHORD_MAP_DEFAULT = 0, CHORD_MAP_EXTENDED, CHORD_MAP_CHROMATIC };
enum ChordDirection : uint8_t {
    CHORD_DIR_CENTER = 0, CHORD_DIR_N, CHORD_DIR_NE, CHORD_DIR_E,
    CHORD_DIR_SE, CHORD_DIR_S, CHORD_DIR_SW, CHORD_DIR_W, CHORD_DIR_NW
};
enum ChordBassMode : uint8_t { CHORD_BASS_OFF = 0, CHORD_BASS_ROOT, CHORD_BASS_SLASH };

struct ChordSettings {
    uint8_t key;                 // 0=C .. 11=B
    ChordScale scale;
    ChordMap map;
    int8_t octave;               // MIDI octave, normally 2..6
    ChordBassMode bassMode;
    bool voiceLeading;
    int8_t octaveShift[7];       // per diatonic button, -2..+2
    int8_t inversion[7];         // per button, -2..+3
    uint8_t lockedType[7];       // CHORD_TYPE_COUNT means unlocked
};

struct ChordVoicing {
    uint8_t notes[7];            // chord tones then optional bass
    uint8_t count;
    uint8_t chordToneCount;
    uint8_t root;
    uint8_t bass;
    ChordType type;
};

class ChordEngine {
public:
    ChordEngine();
    void reset();
    ChordVoicing build(const ChordSettings &settings, uint8_t degree,
                       ChordDirection direction = CHORD_DIR_CENTER,
                       uint8_t slashDegree = 0xFF);
    static ChordSettings defaults();
    static ChordType modifiedType(ChordType base, ChordMap map, ChordDirection direction);
    static const char *scaleName(ChordScale scale);
    static const char *typeName(ChordType type);
    static uint8_t intervals(ChordType type, int8_t out[6]);

private:
    uint8_t previous_[6];
    uint8_t previousCount_;
};
