#pragma once

#include <stdint.h>

enum MedoRole : uint8_t { MEDO_DRUM = 0, MEDO_BASS, MEDO_CHORD, MEDO_LEAD, MEDO_SAMPLE, MEDO_ROLE_COUNT };
enum MedoQuantize : uint8_t { MEDO_AS_RECORDED = 0, MEDO_SNAP_16, MEDO_GROOVE };
enum MedoScale : uint8_t { MEDO_NATURAL = 0, MEDO_PENTATONIC_MAJOR, MEDO_PENTATONIC_MINOR, MEDO_SCALE_COUNT };
enum MedoArpDirection : uint8_t { MEDO_ARP_UP = 0, MEDO_ARP_DOWN, MEDO_ARP_UP_DOWN, MEDO_ARP_RANDOM, MEDO_ARP_COUNT };
enum MedoGesture : uint8_t {
    MEDO_CLICK = 0, MEDO_PRESS, MEDO_SLIDE, MEDO_SLAP,
    MEDO_TILT, MEDO_SHAKE, MEDO_WIGGLE, MEDO_MOVE, MEDO_GESTURE_COUNT
};

struct MedoMidiGesture {
    uint8_t status;
    uint8_t data1;
    uint8_t data2;
};

struct MedoTrackSettings {
    uint8_t volume;
    int8_t octave;
    MedoQuantize quantize;
};

class MedoPerformance {
public:
    MedoPerformance();
    void reset();
    bool setRole(MedoRole role);
    MedoRole role() const { return role_; }
    bool setQuantize(MedoRole role, MedoQuantize mode);
    bool setVolume(MedoRole role, uint8_t volume);
    bool setOctave(MedoRole role, int8_t octave);
    bool setScale(MedoScale scale);
    bool setArpDirection(MedoArpDirection direction);
    bool setArpRate(uint8_t rate);
    void setArpEnabled(bool enabled) { arpEnabled_ = enabled; }
    bool setSharedBars(uint16_t bars);
    MedoScale scale() const { return scale_; }
    MedoArpDirection arpDirection() const { return arpDirection_; }
    uint8_t arpRate() const { return arpRate_; }
    bool arpEnabled() const { return arpEnabled_; }
    uint8_t arpNoteIndex(uint8_t noteCount, uint32_t tick) const;
    uint32_t arpIntervalUs(uint16_t bpm) const;
    uint16_t sharedBars() const { return sharedBars_ == 0 ? 128 : sharedBars_; }
    uint8_t quantizeNote(uint8_t midiNote) const;
    const MedoTrackSettings &settings(MedoRole role) const;
    uint16_t quantizeTick(MedoRole role, uint16_t tick, uint16_t ticksPerBar) const;
    static MedoMidiGesture gestureMidi(MedoGesture gesture, uint8_t value,
                                       uint8_t channel = 0);

private:
    MedoRole role_;
    MedoTrackSettings tracks_[MEDO_ROLE_COUNT];
    MedoScale scale_;
    MedoArpDirection arpDirection_;
    uint8_t arpRate_;
    bool arpEnabled_;
    uint8_t sharedBars_; // zero encodes 128 bars
};
