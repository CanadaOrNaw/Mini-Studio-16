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
    uint8_t arp[2] = {};
    assert(p.arpNotes(chord, ARP_UP, 1, arp) == 1 && arp[0] == 64);
    assert(p.arpNotes(chord, ARP_DOWN, 0, arp) == 1 && arp[0] == 67);
    assert(p.arpNotes(chord, ARP_FINGERPICK, 0, arp) == 2);
    assert(arp[0] == chord.notes[0] && arp[1] == chord.notes[2]);
    assert(hiChordStrumSpacingFrames(HICHORD_STRUM_SLOW, 22050) == 2646);
    assert(hiChordStrumSpacingFrames(HICHORD_STRUM_MEDIUM, 22050) == 1764);
    assert(hiChordStrumSpacingFrames(HICHORD_STRUM_FAST, 22050) == 882);
    assert(hiChordRateIntervalUs(HICHORD_RATE_1_8, 120, 0) == 250000);
    assert(hiChordRateIntervalUs(HICHORD_RATE_1_16T, 120, 0) == 83333);
    assert(hiChordRateIntervalUs(HICHORD_RATE_SWING_8, 120, 0) == 333333);
    assert(hiChordRateIntervalUs(HICHORD_RATE_SWING_8, 120, 1) == 166666);
    assert(hiChordAutoDrumRate(CHORD_DIR_N) == HICHORD_RATE_1_4);
    assert(hiChordAutoDrumRate(CHORD_DIR_SE) == HICHORD_RATE_SWING_16);
    assert(hiChordLoopFrames(0, 120, 22050) == 0);
    assert(hiChordLoopFrames(1, 120, 22050) == 44100);
    assert(hiChordLoopFrames(8, 120, 22050) == 352800);
    assert(hiChordLoopFrames(9, 120, 22050) == 0);
    assert(hiChordCountInFrames(120, 22050) == 44100);
    assert(hiChordHiroWindowMs(0) == 200 && hiChordHiroWindowMs(3) == 50);
    assert(hiChordHiroGrade(2, 2, 0, 0) == HICHORD_HIRO_PERFECT);
    assert(hiChordHiroGrade(2, 2, -70, 0) == HICHORD_HIRO_GREAT);
    assert(hiChordHiroGrade(2, 2, 180, 0) == HICHORD_HIRO_OK);
    assert(hiChordHiroGrade(2, 2, 201, 0) == HICHORD_HIRO_MISS);
    assert(hiChordHiroGrade(2, 3, 0, 0) == HICHORD_HIRO_MISS);
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
