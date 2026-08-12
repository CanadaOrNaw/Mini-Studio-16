#include "../hichord_performance.h"
#include <assert.h>

int main() {
    ChordEngine ce; ChordSettings s = ChordEngine::defaults(); s.voiceLeading = false;
    ChordVoicing chord = ce.build(s, 0);
    HiChordPerformance p;
    assert(p.setMode(HICHORD_ARPEGGIO));
    assert(!p.setMode(static_cast<HiChordMode>(99)));
    HiChordScheduledNote notes[7];
    assert(p.scheduleStrum(chord, false, 100, notes) == chord.count);
    assert(notes[0].note == 60 && notes[1].frameOffset == 100);
    p.scheduleStrum(chord, true, 10, notes);
    assert(notes[0].note == chord.notes[chord.count - 1]);
    assert(p.arpNote(chord, ARP_UP, ARP_CHORD_ONLY, 1) == 64);
    assert(p.arpNote(chord, ARP_DOWN, ARP_CHORD_ONLY, 0) == 67);
    assert(p.arpNote(chord, ARP_BASS_ONLY == ARP_CHORD_ONLY ? ARP_UP : ARP_UP,
                     ARP_BASS_ONLY, 5) == 36);
    HiChordSequenceStep step = {6, CHORD_DIR_SE, 4, 1};
    assert(p.setSequenceStep(15, step));
    assert(p.sequenceStep(15).degree == 6);
    assert(!p.setSequenceStep(16, step));
    for (uint8_t style = 0; style < 7; ++style) for (uint8_t variation = 0; variation < 8; ++variation) {
        uint32_t hash = 0;
        for (uint8_t voice = 0; voice < 7; ++voice) for (uint8_t st = 0; st < 16; ++st)
            hash = hash * 33u + HiChordDrumGrooves::hit(style, variation, voice, st);
        assert(hash != 0);
    }
    assert(hiChordPracticeSongCount() == 16);
    for (uint8_t i = 0; i < 16; ++i) assert(hiChordPracticeSong(i).length > 0);
    HiChordEarTrainer a(42), b(42);
    for (uint8_t i = 0; i < 20; ++i) assert(a.nextDegree(3) == b.nextDegree(3));
    return 0;
}
