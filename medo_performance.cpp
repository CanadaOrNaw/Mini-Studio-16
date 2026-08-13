#include "medo_performance.h"

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

MedoPerformance::MedoPerformance() { reset(); }

void MedoPerformance::reset() {
    role_ = MEDO_DRUM;
    scale_ = MEDO_NATURAL;
    arpDirection_ = MEDO_ARP_UP;
    arpRate_ = 1;
    arpEnabled_ = false;
    sharedBars_ = 1;
    for (uint8_t i = 0; i < MEDO_ROLE_COUNT; ++i) {
        tracks_[i].volume = 100;
        tracks_[i].octave = 0;
        tracks_[i].quantize = MEDO_AS_RECORDED;
    }
}

bool MedoPerformance::setScale(MedoScale scale) {
    if (scale >= MEDO_SCALE_COUNT) return false;
    scale_ = scale; return true;
}
bool MedoPerformance::setArpDirection(MedoArpDirection direction) {
    if (direction >= MEDO_ARP_COUNT) return false;
    arpDirection_ = direction; return true;
}
bool MedoPerformance::setArpRate(uint8_t rate) {
    if (rate != 1 && rate != 2 && rate != 4 && rate != 8) return false;
    arpRate_ = rate; return true;
}

uint8_t MedoPerformance::arpNoteIndex(uint8_t noteCount, uint32_t tick) const {
    if (noteCount == 0) return 0;
    switch (arpDirection_) {
        case MEDO_ARP_DOWN:
            return static_cast<uint8_t>(noteCount - 1u - tick % noteCount);
        case MEDO_ARP_UP_DOWN: {
            if (noteCount == 1) return 0;
            const uint8_t span = static_cast<uint8_t>(noteCount * 2u - 2u);
            const uint8_t index = static_cast<uint8_t>(tick % span);
            return index < noteCount ? index : static_cast<uint8_t>(span - index);
        }
        case MEDO_ARP_RANDOM:
            // A2-P2: see hichord_performance.cpp — same LCG-at-tick defect.
            return static_cast<uint8_t>(scrambleTick(tick) % noteCount);
        case MEDO_ARP_UP:
        default:
            return static_cast<uint8_t>(tick % noteCount);
    }
}

uint32_t MedoPerformance::arpIntervalUs(uint16_t bpm) const {
    if (bpm == 0 || arpRate_ == 0) return 0;
    return 60000000UL / bpm / arpRate_;
}
bool MedoPerformance::setSharedBars(uint16_t bars) {
    if (bars == 0 || bars > 128) return false;
    sharedBars_ = static_cast<uint8_t>(bars == 128 ? 0 : bars); return true;
}

uint8_t MedoPerformance::quantizeNote(uint8_t note) const {
    if (note > 127 || scale_ == MEDO_NATURAL) return note;
    static const uint16_t masks[2] = {
        static_cast<uint16_t>((1u<<0)|(1u<<2)|(1u<<4)|(1u<<7)|(1u<<9)),
        static_cast<uint16_t>((1u<<0)|(1u<<3)|(1u<<5)|(1u<<7)|(1u<<10))
    };
    const uint16_t mask = masks[scale_ - MEDO_PENTATONIC_MAJOR];
    const uint8_t octave = note / 12u, pitch = note % 12u;
    for (uint8_t distance = 0; distance < 12; ++distance) {
        const uint8_t candidate = static_cast<uint8_t>((pitch + 12u - distance) % 12u);
        if (mask & (1u << candidate)) {
            int value = octave * 12 + candidate;
            if (candidate > pitch) value -= 12;
            return static_cast<uint8_t>(value < 0 ? 0 : value);
        }
    }
    return note;
}

bool MedoPerformance::setRole(MedoRole role) {
    if (role >= MEDO_ROLE_COUNT) return false;
    role_ = role;
    return true;
}
bool MedoPerformance::setQuantize(MedoRole role, MedoQuantize mode) {
    if (role >= MEDO_ROLE_COUNT || mode > MEDO_GROOVE) return false;
    tracks_[role].quantize = mode; return true;
}
bool MedoPerformance::setVolume(MedoRole role, uint8_t volume) {
    if (role >= MEDO_ROLE_COUNT || volume > 127) return false;
    tracks_[role].volume = volume; return true;
}
bool MedoPerformance::setOctave(MedoRole role, int8_t octave) {
    if (role >= MEDO_ROLE_COUNT || octave < -4 || octave > 4) return false;
    tracks_[role].octave = octave; return true;
}
const MedoTrackSettings &MedoPerformance::settings(MedoRole role) const {
    return tracks_[role < MEDO_ROLE_COUNT ? role : MEDO_DRUM];
}

uint16_t MedoPerformance::quantizeTick(MedoRole role, uint16_t tick,
                                       uint16_t ticksPerBar) const {
    if (role >= MEDO_ROLE_COUNT || ticksPerBar < 16) return tick;
    const MedoQuantize mode = tracks_[role].quantize;
    if (mode == MEDO_AS_RECORDED) return tick;
    const uint16_t step = ticksPerBar / 16;
    uint16_t snapped = static_cast<uint16_t>(((uint32_t)tick + step / 2) / step * step);
    if (snapped >= ticksPerBar) snapped = 0;
    if (mode == MEDO_GROOVE) {
        const uint16_t index = static_cast<uint16_t>(snapped / step);
        if ((index & 1u) != 0u) {
            const uint16_t delay = step / 6; // stable MEDO-style swung offbeat
            snapped = static_cast<uint16_t>((snapped + delay) % ticksPerBar);
        }
    }
    return snapped;
}

MedoMidiGesture MedoPerformance::gestureMidi(MedoGesture gesture, uint8_t value,
                                              uint8_t channel) {
    if (value > 127) value = 127;
    channel &= 0x0F;
    MedoMidiGesture out = { static_cast<uint8_t>(0xB0 | channel), 0, value };
    switch (gesture) {
        case MEDO_CLICK:  out.status = static_cast<uint8_t>(0x90 | channel); out.data1 = 60; break;
        case MEDO_PRESS:  out.status = static_cast<uint8_t>(0xD0 | channel); out.data1 = value; out.data2 = 0; break;
        case MEDO_SLIDE:  out.data1 = 11; break;       // expression/vertical slide
        case MEDO_SLAP:   out.status = static_cast<uint8_t>(0x90 | channel); out.data1 = 39; break;
        case MEDO_TILT:   out.data1 = 1; break;
        case MEDO_SHAKE:  out.status = static_cast<uint8_t>(0x90 | channel); out.data1 = 69; break;
        case MEDO_WIGGLE: {
            const uint16_t bend = static_cast<uint16_t>(value) << 7;
            out.status = static_cast<uint8_t>(0xE0 | channel);
            out.data1 = static_cast<uint8_t>(bend & 0x7F);
            out.data2 = static_cast<uint8_t>((bend >> 7) & 0x7F);
            break;
        }
        case MEDO_MOVE:   out.data1 = 113; break;
        default:          out.data1 = 0; out.data2 = 0; break;
    }
    return out;
}
