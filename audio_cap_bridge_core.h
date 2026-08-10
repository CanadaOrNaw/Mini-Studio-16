#pragma once

#include "audio_cap_protocol.h"
#include "pcm_ring.h"

#include <stddef.h>
#include <stdint.h>

// Hardware-independent state shared by the Cardputer bridge tests and the
// original-ESP32 Audio Cap firmware. Everything is fixed-size: these methods
// are safe to call from real-time callbacks and never allocate.
struct AudioCapBridgeStats {
    uint32_t packetsBuilt;
    uint32_t packetsAccepted;
    uint32_t crcErrors;
    uint32_t sequenceGaps;
    uint32_t playbackUnderruns;
    uint32_t captureOverruns;
    uint32_t clippedSamples;
};

class AudioCapBridgeCore {
public:
    AudioCapBridgeCore();

    void reset();
    size_t pushPlayback22050(const int16_t* mono, size_t frames);
    size_t popCapture22050(int16_t* mono, size_t frames);
    void buildHostPacket(AudioCapPacket& packet, uint16_t commands = 0);
    bool acceptCapPacket(const AudioCapPacket& packet);

    // Cap-side conversion helpers. Playback is exact 2x linear interpolation
    // to 44.1 kHz stereo. Capture uses a short symmetric low-pass FIR before
    // exact 2:1 stereo-to-mono decimation.
    size_t playback22050ToStereo44100(const int16_t* input, size_t frames,
                                      int16_t* interleaved, size_t outputFrames);
    size_t captureStereo44100To22050(const int16_t* interleaved, size_t frames,
                                     int16_t* mono, size_t outputFrames);

    const AudioCapBridgeStats& stats() const { return _stats; }
    uint16_t remoteStatus() const { return _remoteStatus; }
    uint8_t remoteFlags() const { return _remoteFlags; }
    uint32_t playbackBuffered() const { return _playback.size(); }
    uint32_t captureBuffered() const { return _capture.size(); }

private:
    static int16_t clamp16(int32_t sample, uint32_t& clipped);

    SpscRing<int16_t, 1024> _playback;
    SpscRing<int16_t, 1024> _capture;
    AudioCapBridgeStats _stats;
    uint32_t _txSequence;
    uint32_t _rxSequence;
    uint16_t _remoteStatus;
    uint8_t _remoteFlags;
    bool _haveRxSequence;
    int16_t _interpolationPrevious;
    bool _haveInterpolationPrevious;
    int16_t _decimationHistory[7];
    uint8_t _decimationCount;
    uint8_t _decimationPhase;
};
