#include "event_looper.h"

#include "config.h"
#include "performance_state.h"

EventLooperCore g_eventLooper;
uint16_t g_eventLoopPosition = 0;
static int8_t s_recordRoleOverride = -1;

void eventLooperSetRecordRoleOverride(int8_t role) {
    s_recordRoleOverride = role >= 0 && role < EVENT_LOOP_TRACKS ? role : -1;
}

void eventLooperInit() {
    g_eventLooper.clearAll();
    g_eventLoopPosition = 0;
}

void eventLooperResetTransport() { g_eventLoopPosition = 0; }

void eventLooperSetPosition(uint16_t step) {
    g_eventLoopPosition = static_cast<uint16_t>(step % EVENT_LOOP_MAX_STEPS);
}

void eventLooperAdvance() {
    g_eventLoopPosition = static_cast<uint16_t>(
        (g_eventLoopPosition + 1u) % EVENT_LOOP_MAX_STEPS);
}

static uint16_t quantizedRoleTick(uint16_t tick, EventLoopRole role) {
    const uint16_t bar = static_cast<uint16_t>(tick / EVENT_LOOP_TICKS_PER_BAR);
    const uint16_t within = static_cast<uint16_t>(tick % EVENT_LOOP_TICKS_PER_BAR);
    return static_cast<uint16_t>(bar * EVENT_LOOP_TICKS_PER_BAR +
        g_medoPerformance.quantizeTick(static_cast<MedoRole>(role), within,
                                       EVENT_LOOP_TICKS_PER_BAR));
}

EventLoopRole eventLooperRoleForSynth(uint8_t synthTrack) {
    if (synthTrack == 0) return EVENT_ROLE_BASS;
    if (synthTrack == 2) return EVENT_ROLE_CHORD;
    return EVENT_ROLE_LEAD;
}

bool eventLooperRecordSynth(uint16_t step, uint8_t synthTrack, uint8_t midiNote,
                            uint8_t velocity) {
    if (synthTrack >= NUM_SYNTHS || midiNote < 12 || midiNote > 127) return false;
    const uint8_t role = s_recordRoleOverride >= 0
        ? static_cast<uint8_t>(s_recordRoleOverride)
        : static_cast<uint8_t>(eventLooperRoleForSynth(synthTrack));
    return g_eventLooper.add(quantizedRoleTick(step, static_cast<EventLoopRole>(role)), role, EVENT_LOOP_NOTE,
                             synthTrack, midiNote, velocity);
}

bool eventLooperRecordSynthRelease(uint16_t step, uint8_t synthTrack,
                                   uint8_t midiNote) {
    if (synthTrack >= NUM_SYNTHS || midiNote < 12 || midiNote > 127) return false;
    const uint8_t role = s_recordRoleOverride >= 0
        ? static_cast<uint8_t>(s_recordRoleOverride)
        : static_cast<uint8_t>(eventLooperRoleForSynth(synthTrack));
    return g_eventLooper.add(quantizedRoleTick(step, static_cast<EventLoopRole>(role)), role, EVENT_LOOP_NOTE,
                             synthTrack, midiNote, 0,
                             EVENT_LOOP_FLAG_NOTE_OFF);
}

bool eventLooperRecordDrum(uint16_t step, uint8_t lane, uint8_t velocity) {
    if (lane >= NUM_DRUM_LANES) return false;
    return g_eventLooper.add(quantizedRoleTick(step, EVENT_ROLE_DRUM), EVENT_ROLE_DRUM, EVENT_LOOP_DRUM,
                             lane, velocity, 0);
}

bool eventLooperRecordSample(uint16_t step, uint8_t slot, uint8_t key,
                             uint8_t velocity) {
    if (slot >= 16 || key >= 16) return false;
    return g_eventLooper.add(quantizedRoleTick(step, EVENT_ROLE_SAMPLE), EVENT_ROLE_SAMPLE, EVENT_LOOP_SAMPLE,
                             slot, key, velocity);
}

bool eventLooperRecordControl(uint16_t step, uint8_t control, uint8_t value) {
    return g_eventLooper.add(quantizedRoleTick(step, EVENT_ROLE_SAMPLE), EVENT_ROLE_SAMPLE, EVENT_LOOP_CONTROL,
                             control, value, 0);
}

const char* eventLooperRoleName(uint8_t track) {
    switch (track) {
        case EVENT_ROLE_DRUM: return "drum";
        case EVENT_ROLE_BASS: return "bass";
        case EVENT_ROLE_CHORD: return "chord";
        case EVENT_ROLE_LEAD: return "lead";
        case EVENT_ROLE_SAMPLE: return "sample";
        default: return "unknown";
    }
}
