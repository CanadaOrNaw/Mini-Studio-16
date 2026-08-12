#include "../chord_engine.h"
#include <assert.h>
#include <string.h>

int main() {
    ChordEngine engine;
    ChordSettings s = ChordEngine::defaults();
    s.voiceLeading = false;
    ChordVoicing c = engine.build(s, 0);
    assert(c.type == CHORD_MAJOR && c.count == 4);
    assert(c.notes[0] == 60 && c.notes[1] == 64 && c.notes[2] == 67 && c.bass == 36);
    ChordVoicing d = engine.build(s, 1);
    assert(d.type == CHORD_MINOR && d.notes[0] == 62 && d.notes[1] == 65 && d.notes[2] == 69);
    ChordVoicing mod = engine.build(s, 0, CHORD_DIR_NE);
    assert(mod.type == CHORD_DOM7 && mod.chordToneCount == 4);
    s.map = CHORD_MAP_CHROMATIC;
    assert(engine.build(s, 0, CHORD_DIR_NW).type == CHORD_MAJ7_SHARP11);
    s.lockedType[0] = CHORD_MIN11;
    assert(engine.build(s, 0).chordToneCount == 6);
    s.bassMode = CHORD_BASS_SLASH;
    assert(engine.build(s, 0, CHORD_DIR_CENTER, 4).bass == 43);
    s.octaveShift[0] = 99; s.inversion[0] = 99; s.key = 255;
    c = engine.build(s, 0);
    for (uint8_t i = 0; i < c.count; ++i) assert(c.notes[i] <= 127);
    assert(strcmp(ChordEngine::scaleName(SCALE_BLUES), "BLUES") == 0);
    assert(strcmp(ChordEngine::typeName(CHORD_DOM7_ALT), "7ALT") == 0);
    for (uint8_t t = 0; t < CHORD_TYPE_COUNT; ++t) {
        int8_t iv[6]; const uint8_t n = ChordEngine::intervals(static_cast<ChordType>(t), iv);
        assert(n >= 3 && n <= 6);
        for (uint8_t i = 1; i < n; ++i) assert(iv[i] > iv[i - 1]);
    }
    return 0;
}
