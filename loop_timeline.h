#pragma once

#include <stdint.h>

enum LoopTrackState : uint8_t {
    LOOP_TRACK_EMPTY = 0,
    LOOP_TRACK_ARMED,
    LOOP_TRACK_WAITING,
    LOOP_TRACK_RECORDING,
    LOOP_TRACK_PLAYING,
    LOOP_TRACK_MUTED,
    LOOP_TRACK_ERROR,
};

class LoopTimeline {
public:
    LoopTimeline() : _lengthFrames(0) {}
    bool establish(uint32_t frames) {
        if (frames == 0 || _lengthFrames != 0) return false;
        _lengthFrames = frames;
        return true;
    }
    uint32_t lengthFrames() const { return _lengthFrames; }
    uint32_t position(uint32_t absoluteFrame) const {
        return _lengthFrames ? absoluteFrame % _lengthFrames : 0;
    }
    uint32_t framesUntilBoundary(uint32_t absoluteFrame) const {
        if (_lengthFrames == 0) return 0;
        const uint32_t remainder = absoluteFrame % _lengthFrames;
        return remainder == 0 ? 0 : _lengthFrames - remainder;
    }

private:
    uint32_t _lengthFrames;
};

class LoopTrackControl {
public:
    explicit LoopTrackControl(uint8_t index)
        : _index(index), _state(LOOP_TRACK_EMPTY), _scheduledFrame(0), _lengthFrames(0) {}

    bool arm() {
        if (_state != LOOP_TRACK_EMPTY && _state != LOOP_TRACK_ERROR) return false;
        _state = LOOP_TRACK_ARMED;
        return true;
    }

    bool requestRecord(uint32_t absoluteFrame, const LoopTimeline& timeline) {
        if (_state != LOOP_TRACK_ARMED) return false;
        if (_index == 0 && timeline.lengthFrames() == 0) {
            _scheduledFrame = absoluteFrame;
            _state = LOOP_TRACK_RECORDING;
            return true;
        }
        if (timeline.lengthFrames() == 0) return false;
        _scheduledFrame = absoluteFrame + timeline.framesUntilBoundary(absoluteFrame);
        _state = _scheduledFrame == absoluteFrame ? LOOP_TRACK_RECORDING : LOOP_TRACK_WAITING;
        return true;
    }

    bool update(uint32_t absoluteFrame) {
        if (_state != LOOP_TRACK_WAITING || absoluteFrame < _scheduledFrame) return false;
        _state = LOOP_TRACK_RECORDING;
        return true;
    }

    bool finishRecording(uint32_t capturedFrames, LoopTimeline& timeline) {
        if (_state != LOOP_TRACK_RECORDING || capturedFrames == 0) return false;
        if (_index == 0 && timeline.lengthFrames() == 0 && !timeline.establish(capturedFrames))
            return false;
        if (timeline.lengthFrames() == 0) return false;
        _lengthFrames = timeline.lengthFrames();
        _state = LOOP_TRACK_PLAYING;
        return true;
    }

    bool toggleMute() {
        if (_state == LOOP_TRACK_PLAYING) { _state = LOOP_TRACK_MUTED; return true; }
        if (_state == LOOP_TRACK_MUTED) { _state = LOOP_TRACK_PLAYING; return true; }
        return false;
    }

    void fail() { _state = LOOP_TRACK_ERROR; }
    LoopTrackState state() const { return _state; }
    uint32_t scheduledFrame() const { return _scheduledFrame; }
    uint32_t lengthFrames() const { return _lengthFrames; }

private:
    uint8_t _index;
    LoopTrackState _state;
    uint32_t _scheduledFrame;
    uint32_t _lengthFrames;
};
