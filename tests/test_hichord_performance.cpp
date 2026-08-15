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
    // A2 test gap: this used to compute a hash and assert only `hash != 0`,
    // which passes if a single one of the 112 cells is set. Assert that all
    // 56 grooves are genuinely DISTINCT — that is the actual README claim.
    {
        uint32_t hashes[56];
        uint8_t count = 0;
        for (uint8_t style = 0; style < 7; ++style)
            for (uint8_t variation = 0; variation < 8; ++variation) {
                uint32_t hash = 2166136261u;
                uint8_t hits = 0;
                for (uint8_t voice = 0; voice < 7; ++voice)
                    for (uint8_t st = 0; st < 16; ++st) {
                        const uint8_t hit = HiChordDrumGrooves::hit(style, variation, voice, st);
                        hash = (hash ^ hit) * 16777619u;
                        hits = static_cast<uint8_t>(hits + (hit ? 1 : 0));
                    }
                assert(hits >= 4);            // a groove has to actually play
                hashes[count++] = hash;
            }
        assert(count == 56);
        for (uint8_t i = 0; i < count; ++i)
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < count; ++j)
                assert(hashes[i] != hashes[j]);
    }

    // A2 test gap: ARP_RANDOM, ARP_UP_DOWN and ARP_DOWN_UP were never
    // called at all. Walk a full cycle of every pattern and assert it stays
    // in range, and that each pattern produces a distinct sequence.
    {
        ChordSettings full = ChordEngine::defaults();
        full.voiceLeading = false;
        ChordEngine engine;
        const ChordVoicing quad = engine.build(full, 1);   // a 4-note chord
        assert(quad.count >= 3);
        uint32_t patternHash[ARP_PATTERN_COUNT];
        for (uint8_t pattern = 0; pattern < ARP_PATTERN_COUNT; ++pattern) {
            uint32_t hash = 2166136261u;
            uint8_t distinctNotes = 0;
            bool seen[128] = {false};
            for (uint32_t tick = 0; tick < 64; ++tick) {
                uint8_t out[2] = {0, 0};
                const uint8_t produced =
                    p.arpNotes(quad, static_cast<HiChordArpPattern>(pattern), tick, out);
                assert(produced >= 1 && produced <= 2);
                for (uint8_t n = 0; n < produced; ++n) {
                    bool belongs = false;
                    for (uint8_t c = 0; c < quad.count; ++c)
                        if (quad.notes[c] == out[n]) belongs = true;
                    assert(belongs);          // never invents a note
                    if (!seen[out[n]]) { seen[out[n]] = true; ++distinctNotes; }
                    hash = (hash ^ out[n]) * 16777619u;
                }
            }
            // Every pattern must actually move around the chord.
            assert(distinctNotes >= 2);
            patternHash[pattern] = hash;
        }
        for (uint8_t i = 0; i < ARP_PATTERN_COUNT; ++i)
            for (uint8_t j = static_cast<uint8_t>(i + 1); j < ARP_PATTERN_COUNT; ++j)
                assert(patternHash[i] != patternHash[j]);

        // A2-P2 regression: ARP_RANDOM was an LCG evaluated AT the tick, so
        // it decayed to a period-4 sequence — literally ARP_UP shifted by
        // one. It must be deterministic per tick but not periodic at the
        // chord length, and must not equal ARP_UP at any offset.
        uint8_t randomSeq[32], upSeq[32];
        for (uint32_t tick = 0; tick < 32; ++tick) {
            uint8_t out[2] = {0, 0};
            p.arpNotes(quad, ARP_RANDOM, tick, out); randomSeq[tick] = out[0];
            p.arpNotes(quad, ARP_UP, tick, out);     upSeq[tick] = out[0];
        }
        for (uint32_t tick = 0; tick < 32; ++tick) {   // deterministic
            uint8_t out[2] = {0, 0};
            p.arpNotes(quad, ARP_RANDOM, tick, out);
            assert(out[0] == randomSeq[tick]);
        }
        bool periodic = true;
        for (uint32_t tick = 0; tick + quad.count < 32; ++tick)
            if (randomSeq[tick] != randomSeq[tick + quad.count]) periodic = false;
        assert(!periodic);
        for (uint8_t offset = 0; offset < quad.count; ++offset) {
            bool matchesUp = true;
            for (uint32_t tick = 0; tick + offset < 32; ++tick)
                if (randomSeq[tick + offset] != upSeq[tick]) matchesUp = false;
            assert(!matchesUp);
        }
    }
    assert(hiChordPracticeSongCount() == 16);
    for (uint8_t i = 0; i < 16; ++i) assert(hiChordPracticeSong(i).length > 0);
    HiChordEarTrainer a(42), b(42);
    for (uint8_t i = 0; i < 20; ++i) assert(a.nextDegree(3) == b.nextDegree(3));
    return 0;
}
