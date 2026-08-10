#include "audio_cap_bridge_core.h"

#include <string.h>

AudioCapBridgeCore::AudioCapBridgeCore() { reset(); }

void AudioCapBridgeCore::reset() {
    _playback.reset();
    _capture.reset();
    memset(&_stats, 0, sizeof(_stats));
    _txSequence = 0;
    _rxSequence = 0;
    _remoteStatus = 0;
    _remoteFlags = 0;
    _haveRxSequence = false;
    _interpolationPrevious = 0;
    _haveInterpolationPrevious = false;
    memset(_decimationHistory, 0, sizeof(_decimationHistory));
    _decimationCount = 0;
    _decimationPhase = 0;
}

size_t AudioCapBridgeCore::pushPlayback22050(const int16_t* mono, size_t frames) {
    return _playback.push(mono, frames);
}

size_t AudioCapBridgeCore::popCapture22050(int16_t* mono, size_t frames) {
    return _capture.pop(mono, frames);
}

void AudioCapBridgeCore::buildHostPacket(AudioCapPacket& packet, uint16_t commands) {
    int16_t pcm[AUDIO_CAP_FRAMES] = {};
    const size_t frames = _playback.pop(pcm, AUDIO_CAP_FRAMES);
    uint8_t flags = 0;
    if (frames) flags |= AUDIO_CAP_PCM_VALID;
    if (frames < AUDIO_CAP_FRAMES) {
        flags |= AUDIO_CAP_UNDERRUN;
        ++_stats.playbackUnderruns;
    }
    audioCapPacketInit(packet, _txSequence++, flags, pcm,
                       static_cast<uint16_t>(frames), commands);
    ++_stats.packetsBuilt;
}

bool AudioCapBridgeCore::acceptCapPacket(const AudioCapPacket& packet) {
    if (!audioCapPacketValidate(packet)) {
        ++_stats.crcErrors;
        return false;
    }
    if (_haveRxSequence && !audioCapSequenceFollows(_rxSequence, packet.sequence))
        ++_stats.sequenceGaps;
    _rxSequence = packet.sequence;
    _haveRxSequence = true;
    _remoteStatus = packet.status;
    _remoteFlags = packet.flags;
    if ((packet.flags & AUDIO_CAP_PCM_VALID) != 0 && packet.frames) {
        int16_t alignedPcm[AUDIO_CAP_FRAMES];
        memcpy(alignedPcm, packet.pcm, packet.frames * sizeof(alignedPcm[0]));
        const size_t pushed = _capture.push(alignedPcm, packet.frames);
        if (pushed != packet.frames) {
            _stats.captureOverruns += static_cast<uint32_t>(packet.frames - pushed);
        }
    }
    ++_stats.packetsAccepted;
    return true;
}

int16_t AudioCapBridgeCore::clamp16(int32_t sample, uint32_t& clipped) {
    if (sample > 32767) {
        ++clipped;
        return 32767;
    }
    if (sample < -32768) {
        ++clipped;
        return -32768;
    }
    return static_cast<int16_t>(sample);
}

size_t AudioCapBridgeCore::playback22050ToStereo44100(
    const int16_t* input, size_t frames, int16_t* interleaved, size_t outputFrames) {
    if (!input || !interleaved || frames == 0 || outputFrames == 0) return 0;
    size_t produced = 0;
    for (size_t index = 0; index < frames && produced + 1 < outputFrames; ++index) {
        const int16_t current = input[index];
        const int16_t previous = _haveInterpolationPrevious
            ? _interpolationPrevious : current;
        const int16_t midpoint = static_cast<int16_t>(
            (static_cast<int32_t>(previous) + static_cast<int32_t>(current)) / 2);
        interleaved[produced * 2] = midpoint;
        interleaved[produced * 2 + 1] = midpoint;
        ++produced;
        interleaved[produced * 2] = current;
        interleaved[produced * 2 + 1] = current;
        ++produced;
        _interpolationPrevious = current;
        _haveInterpolationPrevious = true;
    }
    return produced;
}

size_t AudioCapBridgeCore::captureStereo44100To22050(
    const int16_t* interleaved, size_t frames, int16_t* mono, size_t outputFrames) {
    if (!interleaved || !mono || outputFrames == 0) return 0;
    // Q8 coefficients: [8, 24, 56, 80, 56, 24, 8] / 256. This bounded
    // low-pass is intentionally modest; real analogue/RF performance remains
    // a prototype measurement, while alias rejection is testable on the host.
    static const int16_t coefficients[7] = {8, 24, 56, 80, 56, 24, 8};
    size_t produced = 0;
    for (size_t frame = 0; frame < frames && produced < outputFrames; ++frame) {
        const int32_t left = interleaved[frame * 2];
        const int32_t right = interleaved[frame * 2 + 1];
        const int16_t sample = static_cast<int16_t>((left + right) / 2);
        for (uint8_t index = 0; index < 6; ++index)
            _decimationHistory[index] = _decimationHistory[index + 1];
        _decimationHistory[6] = sample;
        if (_decimationCount < 7) ++_decimationCount;
        _decimationPhase ^= 1u;
        if (_decimationCount < 7 || _decimationPhase != 0) continue;
        int32_t sum = 0;
        for (uint8_t index = 0; index < 7; ++index)
            sum += static_cast<int32_t>(_decimationHistory[index]) * coefficients[index];
        mono[produced++] = clamp16((sum + (sum >= 0 ? 128 : -128)) / 256,
                                   _stats.clippedSamples);
    }
    return produced;
}
