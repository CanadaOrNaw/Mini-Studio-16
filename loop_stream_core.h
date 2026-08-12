#pragma once

#include "pcm_ring.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t LOOP_STREAM_TRACKS = 6;
static const uint8_t LOOP_NO_TRACK = 0xFF;

enum LoopStreamState : uint8_t {
    LOOP_STREAM_EMPTY = 0,
    LOOP_STREAM_PREPARING,
    LOOP_STREAM_RECORD_WAIT,
    LOOP_STREAM_RECORDING,
    LOOP_STREAM_FINALIZING,
    LOOP_STREAM_PLAY_WAIT,
    LOOP_STREAM_PLAYING,
    LOOP_STREAM_MUTED,
    LOOP_STREAM_UNDERRUN,
    LOOP_STREAM_ERROR,
};

struct LoopStreamTrackSnapshot {
    LoopStreamState state;
    uint32_t lengthFrames;
    uint32_t scheduledFrame;
    uint32_t capturedFrames;
    uint32_t droppedFrames;
    uint32_t underruns;
    uint32_t ringFrames;
    int16_t volumeQ15;
};

template <uint32_t PlaybackFrames, uint32_t RecordFrames>
class LoopStreamCore {
public:
    LoopStreamCore() { reset(); }

    void reset() {
        store(&_absoluteFrame, 0);
        store(&_timelineFrames, 0);
        store(&_recordTrack, LOOP_NO_TRACK);
        _recordRing.reset();
        for (uint8_t index = 0; index < LOOP_STREAM_TRACKS; ++index) {
            Track& track = _tracks[index];
            track.playback.reset();
            store(&track.state, LOOP_STREAM_EMPTY);
            store(&track.lengthFrames, 0);
            store(&track.scheduledFrame, 0);
            store(&track.capturedFrames, 0);
            store(&track.droppedFrames, 0);
            store(&track.underruns, 0);
            store(&track.volumeQ15, 32767);
            store(&track.mutedFlag, 0);
            store(&track.lastOutput, 0);
        }
    }

    uint32_t absoluteFrame() const { return load(&_absoluteFrame); }
    uint32_t timelineFrames() const { return load(&_timelineFrames); }
    uint8_t recordTrack() const { return static_cast<uint8_t>(load(&_recordTrack)); }

    bool establishTimeline(uint32_t frames) {
        if (frames == 0) return false;
        uint32_t expected = 0;
        return compareExchange(&_timelineFrames, expected, frames) || expected == frames;
    }

    static uint32_t nextBoundary(uint32_t frame, uint32_t length, bool strictlyNext) {
        if (length == 0) return frame;
        const uint32_t remainder = frame % length;
        if (remainder == 0) return strictlyNext ? frame + length : frame;
        return frame + (length - remainder);
    }

    bool preparePlayback(uint8_t index, uint32_t lengthFrames) {
        if (!validTrack(index) || lengthFrames == 0) return false;
        const LoopStreamState state = trackState(index);
        if (isRecordingState(state)) return false;
        Track& track = _tracks[index];
        store(&track.state, LOOP_STREAM_PREPARING);
        track.playback.reset();
        store(&track.lengthFrames, lengthFrames);
        store(&track.scheduledFrame, 0);
        return true;
    }

    size_t pushPlayback(uint8_t index, const int16_t* frames, size_t count) {
        if (!validTrack(index)) return 0;
        const LoopStreamState state = trackState(index);
        if (state != LOOP_STREAM_PREPARING && state != LOOP_STREAM_PLAY_WAIT &&
            state != LOOP_STREAM_PLAYING && state != LOOP_STREAM_MUTED)
            return 0;
        return _tracks[index].playback.push(frames, count);
    }

    uint32_t playbackFree(uint8_t index) const {
        return validTrack(index) ? _tracks[index].playback.freeSpace() : 0;
    }

    bool armPlayback(uint8_t index, uint32_t scheduledFrame) {
        if (!validTrack(index) || trackState(index) != LOOP_STREAM_PREPARING ||
            _tracks[index].playback.empty())
            return false;
        store(&_tracks[index].scheduledFrame, scheduledFrame);
        store(&_tracks[index].state, LOOP_STREAM_PLAY_WAIT);
        return true;
    }

    bool beginRecording(uint8_t index, uint32_t scheduledFrame, uint32_t targetFrames) {
        if (!validTrack(index) || recordTrack() != LOOP_NO_TRACK) return false;
        const LoopStreamState state = trackState(index);
        if (state != LOOP_STREAM_EMPTY && state != LOOP_STREAM_ERROR) return false;
        if (index > 0 && (timelineFrames() == 0 || targetFrames != timelineFrames()))
            return false;

        _recordRing.reset();
        Track& track = _tracks[index];
        store(&track.capturedFrames, 0);
        store(&track.droppedFrames, 0);
        store(&track.underruns, 0);
        store(&track.mutedFlag, 0);
        store(&track.scheduledFrame, scheduledFrame);
        store(&track.lengthFrames, targetFrames);
        store(&_recordTrack, index);
        store(&track.state, frameReached(absoluteFrame(), scheduledFrame)
                                ? LOOP_STREAM_RECORDING : LOOP_STREAM_RECORD_WAIT);
        return true;
    }

    bool requestStopRecording(uint8_t index) {
        if (!validTrack(index) || recordTrack() != index) return false;
        Track& track = _tracks[index];
        uint32_t state = load(&track.state);
        while (state == LOOP_STREAM_RECORD_WAIT || state == LOOP_STREAM_RECORDING) {
            uint32_t expected = state;
            if (compareExchange(&track.state, expected, LOOP_STREAM_FINALIZING)) {
                store(&_recordTrack, LOOP_NO_TRACK);
                return true;
            }
            state = expected;
        }
        return false;
    }

    size_t popRecorded(int16_t* frames, size_t count) {
        return _recordRing.pop(frames, count);
    }
    uint32_t recordedAvailable() const { return _recordRing.size(); }

    bool completeRecording(uint8_t index, uint32_t fileFrames) {
        if (!validTrack(index) || trackState(index) != LOOP_STREAM_FINALIZING ||
            fileFrames == 0)
            return false;
        Track& track = _tracks[index];
        store(&track.lengthFrames, fileFrames);
        track.playback.reset();
        store(&track.state, LOOP_STREAM_PREPARING);
        return true;
    }

    bool prepareResync(uint8_t index) {
        if (!validTrack(index) || trackState(index) != LOOP_STREAM_UNDERRUN) return false;
        Track& track = _tracks[index];
        store(&track.state, LOOP_STREAM_PREPARING);
        track.playback.reset();
        return true;
    }

    bool setMuted(uint8_t index, bool muted) {
        if (!validTrack(index)) return false;
        Track& track = _tracks[index];
        const LoopStreamState state = trackState(index);
        // P3 (reconciliation report): mute intent used to live only in the
        // PLAYING<->MUTED state pair, so an underrun resync silently
        // unmuted the track (and muting during PLAY_WAIT/UNDERRUN was
        // refused). The intent now lives in mutedFlag, which the audio task
        // applies at every PLAY_WAIT -> playing transition.
        if (state != LOOP_STREAM_PLAY_WAIT && state != LOOP_STREAM_PLAYING &&
            state != LOOP_STREAM_MUTED && state != LOOP_STREAM_UNDERRUN &&
            state != LOOP_STREAM_PREPARING)
            return false;
        store(&track.mutedFlag, muted ? 1u : 0u);
        uint32_t expected = muted ? LOOP_STREAM_PLAYING : LOOP_STREAM_MUTED;
        const uint32_t desired = muted ? LOOP_STREAM_MUTED : LOOP_STREAM_PLAYING;
        compareExchange(&track.state, expected, desired);
        return true;
    }

    bool muted(uint8_t index) const {
        return validTrack(index) && load(&_tracks[index].mutedFlag) != 0;
    }

    bool setVolumeQ15(uint8_t index, int16_t volume) {
        if (!validTrack(index) || volume < 0) return false;
        store(&_tracks[index].volumeQ15, static_cast<uint16_t>(volume));
        return true;
    }

    bool clearTrack(uint8_t index) {
        if (!validTrack(index)) return false;
        const LoopStreamState state = trackState(index);
        if (isRecordingState(state)) return false;
        Track& track = _tracks[index];
        store(&track.state, LOOP_STREAM_EMPTY);
        // P2-5 (reconciliation report): deliberately NOT resetting the
        // playback ring here. The audio task may have sampled the state as
        // PLAYING/MUTED for the current frame and be inside popOne(), and
        // a producer-side reset would write the consumer-owned read index
        // concurrently. Stale ring contents are harmless: only playing
        // states pop, and preparePlayback() resets the ring (from a state
        // where the consumer provably is not popping) before any reuse.
        store(&track.lengthFrames, 0);
        store(&track.scheduledFrame, 0);
        store(&track.capturedFrames, 0);
        store(&track.droppedFrames, 0);
        store(&track.underruns, 0);
        store(&track.mutedFlag, 0);
        if (index == 0) store(&_timelineFrames, 0);
        return true;
    }

    void markError(uint8_t index) {
        if (!validTrack(index)) return;
        store(&_tracks[index].state, LOOP_STREAM_ERROR);
        if (recordTrack() == index) store(&_recordTrack, LOOP_NO_TRACK);
    }

    // Called exactly once per audio frame. Returns the signed sum of all loop
    // tracks after per-track volume; the caller performs the final master clip.
    int32_t processFrame(int16_t dryInput) {
        const uint32_t now = absoluteFrame();
        int32_t contribution = 0;

        for (uint8_t index = 0; index < LOOP_STREAM_TRACKS; ++index) {
            Track& track = _tracks[index];
            LoopStreamState state = static_cast<LoopStreamState>(load(&track.state));
            int32_t trackOutput = 0;
            if (state == LOOP_STREAM_PLAY_WAIT &&
                frameReached(now, load(&track.scheduledFrame))) {
                // Apply the stored mute intent so a resync after an
                // underrun cannot silently unmute the track (P3).
                const LoopStreamState resumed = load(&track.mutedFlag)
                    ? LOOP_STREAM_MUTED : LOOP_STREAM_PLAYING;
                store(&track.state, resumed);
                state = resumed;
            } else if (state == LOOP_STREAM_RECORD_WAIT &&
                       frameReached(now, load(&track.scheduledFrame))) {
                store(&track.state, LOOP_STREAM_RECORDING);
                state = LOOP_STREAM_RECORDING;
            }

            if (state == LOOP_STREAM_PLAYING || state == LOOP_STREAM_MUTED) {
                int16_t sample = 0;
                if (!track.playback.popOne(sample)) {
                    add(&track.underruns, 1);
                    store(&track.state, LOOP_STREAM_UNDERRUN);
                } else if (state == LOOP_STREAM_PLAYING) {
                    const int32_t scaled = static_cast<int32_t>(sample) *
                                           static_cast<int32_t>(load(&track.volumeQ15));
                    trackOutput = scaled / 32767;
                    contribution += trackOutput;
                }
            }
            store(&track.lastOutput, static_cast<uint32_t>(trackOutput));

            if (state == LOOP_STREAM_RECORDING && recordTrack() == index) {
                if (!_recordRing.pushOne(dryInput)) add(&track.droppedFrames, 1);
                const uint32_t captured = add(&track.capturedFrames, 1);
                const uint32_t target = load(&track.lengthFrames);
                if (target > 0 && captured >= target) {
                    store(&track.state, LOOP_STREAM_FINALIZING);
                    store(&_recordTrack, LOOP_NO_TRACK);
                }
            }
        }

        store(&_absoluteFrame, now + 1);
        return contribution;
    }

    LoopStreamState trackState(uint8_t index) const {
        return validTrack(index)
            ? static_cast<LoopStreamState>(load(&_tracks[index].state))
            : LOOP_STREAM_ERROR;
    }
    int32_t lastTrackOutput(uint8_t index) const {
        return validTrack(index) ? static_cast<int32_t>(load(&_tracks[index].lastOutput)) : 0;
    }

    LoopStreamTrackSnapshot snapshot(uint8_t index) const {
        LoopStreamTrackSnapshot result = {};
        if (!validTrack(index)) { result.state = LOOP_STREAM_ERROR; return result; }
        const Track& track = _tracks[index];
        result.state = static_cast<LoopStreamState>(load(&track.state));
        result.lengthFrames = load(&track.lengthFrames);
        result.scheduledFrame = load(&track.scheduledFrame);
        result.capturedFrames = load(&track.capturedFrames);
        result.droppedFrames = load(&track.droppedFrames);
        result.underruns = load(&track.underruns);
        result.ringFrames = track.playback.size();
        result.volumeQ15 = static_cast<int16_t>(load(&track.volumeQ15));
        return result;
    }

private:
    struct Track {
        SpscRing<int16_t, PlaybackFrames> playback;
        alignas(4) uint32_t state;
        alignas(4) uint32_t lengthFrames;
        alignas(4) uint32_t scheduledFrame;
        alignas(4) uint32_t capturedFrames;
        alignas(4) uint32_t droppedFrames;
        alignas(4) uint32_t underruns;
        alignas(4) uint32_t volumeQ15;
        alignas(4) uint32_t mutedFlag;
        alignas(4) uint32_t lastOutput;
    };

    Track _tracks[LOOP_STREAM_TRACKS];
    SpscRing<int16_t, RecordFrames> _recordRing;
    alignas(4) uint32_t _absoluteFrame;
    alignas(4) uint32_t _timelineFrames;
    alignas(4) uint32_t _recordTrack;

    static bool validTrack(uint8_t index) { return index < LOOP_STREAM_TRACKS; }
    static bool frameReached(uint32_t now, uint32_t target) {
        return static_cast<int32_t>(now - target) >= 0;
    }
    static bool isRecordingState(LoopStreamState state) {
        return state == LOOP_STREAM_RECORD_WAIT || state == LOOP_STREAM_RECORDING ||
               state == LOOP_STREAM_FINALIZING;
    }
    static uint32_t load(const uint32_t* value) {
        return __atomic_load_n(value, __ATOMIC_ACQUIRE);
    }
    static void store(uint32_t* value, uint32_t replacement) {
        __atomic_store_n(value, replacement, __ATOMIC_RELEASE);
    }
    static uint32_t add(uint32_t* value, uint32_t increment) {
        return __atomic_add_fetch(value, increment, __ATOMIC_RELAXED);
    }
    static bool compareExchange(uint32_t* value, uint32_t& expected, uint32_t desired) {
        return __atomic_compare_exchange_n(value, &expected, desired, false,
                                           __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    }
};
