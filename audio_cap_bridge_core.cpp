#include "audio_cap_bridge_core.h"

#include <string.h>

namespace {
uint8_t commandBits(uint8_t flags) {
    return flags & (AUDIO_CAP_CMD_PAIR | AUDIO_CAP_CMD_DISCONNECT |
                    AUDIO_CAP_CMD_CLEAR);
}

int16_t clamp16(int32_t sample) {
    if (sample > 32767) return 32767;
    if (sample < -32768) return -32768;
    return static_cast<int16_t>(sample);
}

void updateSequence(uint32_t sequence, uint32_t& next, bool& have,
                    AudioCapBridgeStats& stats) {
    if (have && sequence != next) ++stats.sequenceGaps;
    next = sequence + 1u;
    have = true;
}
}  // namespace

AudioCapHostCore::AudioCapHostCore() { reset(); }

void AudioCapHostCore::reset() {
    _playback.reset();
    _capture.reset();
    memset(&_stats, 0, sizeof(_stats));
    _nextTx = 0;
    _nextRx = 0;
    _haveRx = false;
    _remoteStatus = 0;
}

size_t AudioCapHostCore::pushPlayback(const int16_t* pcm, size_t frames) {
    const size_t pushed = _playback.push(pcm, frames);
    _stats.playbackDrops += static_cast<uint32_t>(frames - pushed);
    return pushed;
}

size_t AudioCapHostCore::popCapture(int16_t* pcm, size_t frames) {
    const size_t popped = _capture.pop(pcm, frames);
    _stats.captureUnderruns += static_cast<uint32_t>(frames - popped);
    if (pcm && popped < frames)
        memset(pcm + popped, 0, (frames - popped) * sizeof(*pcm));
    return popped;
}

void AudioCapHostCore::buildTransfer(AudioCapPacket& packet, uint8_t commands,
                                     uint8_t monitorPercent) {
    int16_t pcm[AUDIO_CAP_FRAMES] = {};
    const uint16_t frames = static_cast<uint16_t>(_playback.pop(pcm, AUDIO_CAP_FRAMES));
    _stats.playbackUnderruns += AUDIO_CAP_FRAMES - frames;
    const uint8_t flags = static_cast<uint8_t>(commandBits(commands) |
        (frames ? static_cast<uint8_t>(AUDIO_CAP_PCM_VALID) : 0u));
    audioCapPacketInit(packet, _nextTx++, flags, pcm, frames);
    packet.status = monitorPercent <= 100 ? monitorPercent : 100;
    packet.crc32 = audioCapPacketCrc(packet);
    ++_stats.packetsBuilt;
}

bool AudioCapHostCore::acceptReply(const AudioCapPacket& packet) {
    if (!audioCapPacketValidate(packet)) {
        ++_stats.packetErrors;
        return false;
    }
    updateSequence(packet.sequence, _nextRx, _haveRx, _stats);
    _remoteStatus = packet.status;
    int16_t pcm[AUDIO_CAP_FRAMES];
    memcpy(pcm, packet.pcm, packet.frames * sizeof(pcm[0]));
    const size_t pushed = (packet.flags & AUDIO_CAP_PCM_VALID)
        ? _capture.push(pcm, packet.frames) : 0;
    _stats.captureDrops += static_cast<uint32_t>(packet.frames - pushed);
    ++_stats.packetsAccepted;
    return true;
}

AudioCapDeviceCore::AudioCapDeviceCore() { reset(); }

void AudioCapDeviceCore::reset() {
    _playback.reset();
    _capture.reset();
    memset(&_stats, 0, sizeof(_stats));
    _nextTx = 0;
    _nextRx = 0;
    _haveRx = false;
    _commands = 0;
    _monitorPercent = 0;
}

bool AudioCapDeviceCore::acceptTransfer(const AudioCapPacket& packet) {
    if (!audioCapPacketValidate(packet)) {
        ++_stats.packetErrors;
        return false;
    }
    updateSequence(packet.sequence, _nextRx, _haveRx, _stats);
    _commands |= commandBits(packet.flags);
    _monitorPercent = audioCapPacketMonitor(packet);
    int16_t pcm[AUDIO_CAP_FRAMES];
    memcpy(pcm, packet.pcm, packet.frames * sizeof(pcm[0]));
    const size_t pushed = (packet.flags & AUDIO_CAP_PCM_VALID)
        ? _playback.push(pcm, packet.frames) : 0;
    _stats.playbackDrops += static_cast<uint32_t>(packet.frames - pushed);
    ++_stats.packetsAccepted;
    return true;
}

void AudioCapDeviceCore::buildReply(AudioCapPacket& packet, uint16_t deviceStatus) {
    int16_t pcm[AUDIO_CAP_FRAMES] = {};
    const uint16_t frames = static_cast<uint16_t>(_capture.pop(pcm, AUDIO_CAP_FRAMES));
    _stats.captureUnderruns += AUDIO_CAP_FRAMES - frames;
    audioCapPacketInit(packet, _nextTx++,
                       frames ? static_cast<uint8_t>(AUDIO_CAP_PCM_VALID) : 0u,
                       pcm, frames);
    packet.status = deviceStatus;
    packet.crc32 = audioCapPacketCrc(packet);
    ++_stats.packetsBuilt;
}

size_t AudioCapDeviceCore::pushCapture(const int16_t* pcm, size_t frames) {
    const size_t pushed = _capture.push(pcm, frames);
    _stats.captureDrops += static_cast<uint32_t>(frames - pushed);
    return pushed;
}

size_t AudioCapDeviceCore::popPlayback(int16_t* pcm, size_t frames) {
    const size_t popped = _playback.pop(pcm, frames);
    _stats.playbackUnderruns += static_cast<uint32_t>(frames - popped);
    if (pcm && popped < frames)
        memset(pcm + popped, 0, (frames - popped) * sizeof(*pcm));
    return popped;
}

uint8_t AudioCapDeviceCore::takeCommands() {
    const uint8_t result = _commands;
    _commands = 0;
    return result;
}

AudioCapPlaybackUpsampler::AudioCapPlaybackUpsampler() { reset(); }

void AudioCapPlaybackUpsampler::reset() {
    _previous = 0;
    _havePrevious = false;
}

size_t AudioCapPlaybackUpsampler::process(const int16_t* input, size_t frames,
                                          int16_t* output, size_t capacity) {
    if (!input || !output) return 0;
    size_t written = 0;
    for (size_t index = 0; index < frames && written + 2 <= capacity; ++index) {
        const int16_t current = input[index];
        if (!_havePrevious) {
            _previous = current;
            _havePrevious = true;
        }
        const int16_t midpoint = static_cast<int16_t>(
            (static_cast<int32_t>(_previous) + current) / 2);
        output[written * 2] = midpoint;
        output[written * 2 + 1] = midpoint;
        ++written;
        output[written * 2] = current;
        output[written * 2 + 1] = current;
        ++written;
        _previous = current;
    }
    return written;
}

AudioCapCaptureResampler::AudioCapCaptureResampler() { reset(); }

void AudioCapCaptureResampler::reset() {
    memset(_history, 0, sizeof(_history));
    _historyIndex = 0;
    _phase = 0;
}

size_t AudioCapCaptureResampler::process(const int32_t* input, size_t frames,
                                         int16_t* output, size_t capacity) {
    if (!input || !output) return 0;
    static const int16_t coefficients[15] = {
        110, 154, -201, -1136, -851, 3035, 9157, 12232,
        9157, 3035, -851, -1136, -201, 154, 110
    };
    size_t written = 0;
    for (size_t frame = 0; frame < frames; ++frame) {
        // PCM1808 samples are left-aligned 24-bit values in 32-bit I2S slots.
        const int32_t left = input[frame * 2] >> 16;
        const int32_t right = input[frame * 2 + 1] >> 16;
        _history[_historyIndex] = clamp16((left + right) / 2);
        _historyIndex = static_cast<uint8_t>((_historyIndex + 1u) % 15u);

        _phase += AUDIO_CAP_SAMPLE_RATE;
        if (_phase < 48000u) continue;
        _phase -= 48000u;
        int64_t filtered = 0;
        uint8_t cursor = _historyIndex;
        for (uint8_t tap = 0; tap < 15; ++tap) {
            cursor = cursor == 0 ? 14 : static_cast<uint8_t>(cursor - 1);
            filtered += static_cast<int32_t>(_history[cursor]) * coefficients[tap];
        }
        if (written < capacity)
            output[written++] = clamp16(static_cast<int32_t>(filtered >> 15));
    }
    return written;
}
