#pragma once

#include "audio_cap_protocol.h"
#include "pcm_ring.h"

#include <stddef.h>
#include <stdint.h>

struct AudioCapBridgeStats {
    uint32_t packetsBuilt;
    uint32_t packetsAccepted;
    uint32_t packetErrors;
    uint32_t playbackDrops;
    uint32_t captureDrops;
    uint32_t playbackUnderruns;
    uint32_t captureUnderruns;
    uint32_t sequenceGaps;
};

class AudioCapHostCore {
public:
    AudioCapHostCore();
    size_t pushPlayback(const int16_t* pcm, size_t frames);
    size_t popCapture(int16_t* pcm, size_t frames);
    void buildTransfer(AudioCapPacket& packet, uint8_t commands, uint8_t monitorPercent);
    bool acceptReply(const AudioCapPacket& packet);
    const AudioCapBridgeStats& stats() const { return _stats; }
    uint16_t remoteStatus() const { return _remoteStatus; }
    void reset();

private:
    SpscRing<int16_t, 2048> _playback;
    SpscRing<int16_t, 2048> _capture;
    AudioCapBridgeStats _stats;
    uint32_t _nextTx;
    uint32_t _nextRx;
    bool _haveRx;
    uint16_t _remoteStatus;
};

class AudioCapDeviceCore {
public:
    AudioCapDeviceCore();
    bool acceptTransfer(const AudioCapPacket& packet);
    void buildReply(AudioCapPacket& packet, uint16_t deviceStatus);
    size_t pushCapture(const int16_t* pcm, size_t frames);
    size_t popPlayback(int16_t* pcm, size_t frames);
    uint8_t takeCommands();
    uint8_t monitorPercent() const { return _monitorPercent; }
    const AudioCapBridgeStats& stats() const { return _stats; }
    void reset();

private:
    SpscRing<int16_t, 2048> _playback;
    SpscRing<int16_t, 2048> _capture;
    AudioCapBridgeStats _stats;
    uint32_t _nextTx;
    uint32_t _nextRx;
    bool _haveRx;
    uint8_t _commands;
    uint8_t _monitorPercent;
};

// Fixed-cost streaming converters used by the ATOM cap. Both retain state
// across calls, so output is identical regardless of input chunk boundaries.
class AudioCapPlaybackUpsampler {
public:
    AudioCapPlaybackUpsampler();
    size_t process(const int16_t* mono22050, size_t frames,
                   int16_t* stereo44100, size_t stereoFrameCapacity);
    void reset();
private:
    int16_t _previous;
    bool _havePrevious;
};

class AudioCapCaptureResampler {
public:
    AudioCapCaptureResampler();
    size_t process(const int32_t* stereo48000, size_t stereoFrames,
                   int16_t* mono22050, size_t outputCapacity);
    void reset();
private:
    int16_t _history[15];
    uint8_t _historyIndex;
    uint32_t _phase;
};
