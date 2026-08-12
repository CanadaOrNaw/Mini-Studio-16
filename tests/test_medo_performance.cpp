#include "../medo_performance.h"
#include <assert.h>

int main() {
    MedoPerformance m;
    assert(m.role() == MEDO_DRUM);
    assert(m.setRole(MEDO_CHORD) && m.role() == MEDO_CHORD);
    assert(!m.setRole(static_cast<MedoRole>(99)));
    assert(m.setQuantize(MEDO_CHORD, MEDO_SNAP_16));
    assert(m.quantizeTick(MEDO_CHORD, 25, 96) == 24);
    assert(m.setQuantize(MEDO_CHORD, MEDO_GROOVE));
    assert(m.quantizeTick(MEDO_CHORD, 7, 96) == 7); // step 1 + 1-tick groove delay
    assert(m.setOctave(MEDO_BASS, -2));
    assert(!m.setOctave(MEDO_BASS, 8));
    assert(m.setVolume(MEDO_LEAD, 127));
    assert(!m.setVolume(MEDO_LEAD, 128));
    assert(m.setScale(MEDO_PENTATONIC_MINOR));
    assert(m.quantizeNote(62) == 60);
    assert(m.setArpDirection(MEDO_ARP_RANDOM));
    assert(m.setArpRate(8));
    assert(m.setSharedBars(128));
    assert(m.sharedBars() == 128);
    assert(!m.setArpRate(3));
    MedoMidiGesture g = MedoPerformance::gestureMidi(MEDO_TILT, 99, 2);
    assert(g.status == 0xB2 && g.data1 == 1 && g.data2 == 99);
    g = MedoPerformance::gestureMidi(MEDO_WIGGLE, 64);
    assert(g.status == 0xE0 && g.data1 == 0 && g.data2 == 64);
    g = MedoPerformance::gestureMidi(MEDO_SHAKE, 127);
    assert(g.status == 0x90 && g.data1 == 69 && g.data2 == 127);
    g = MedoPerformance::gestureMidi(MEDO_SLAP, 88);
    assert(g.data1 == 39);
    g = MedoPerformance::gestureMidi(MEDO_MOVE, 55);
    assert(g.data1 == 113);
    return 0;
}
