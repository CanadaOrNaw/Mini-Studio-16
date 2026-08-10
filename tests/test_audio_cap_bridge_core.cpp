#include "../audio_cap_bridge_core.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

int main() {
    AudioCapBridgeCore bridge;
    int16_t ramp[AUDIO_CAP_FRAMES];
    for (uint16_t i = 0; i < AUDIO_CAP_FRAMES; ++i)
        ramp[i] = static_cast<int16_t>(-16000 + i * 250);
    assert(bridge.pushPlayback22050(ramp, AUDIO_CAP_FRAMES) == AUDIO_CAP_FRAMES);

    AudioCapPacket outgoing = {};
    bridge.buildHostPacket(outgoing, AUDIO_CAP_COMMAND_PAIR);
    assert(audioCapPacketValidate(outgoing));
    assert(outgoing.status == AUDIO_CAP_COMMAND_PAIR);
    assert(outgoing.frames == AUDIO_CAP_FRAMES);
    assert(std::memcmp(outgoing.pcm, ramp, sizeof(ramp)) == 0);

    int16_t stereo[AUDIO_CAP_FRAMES * 4] = {};
    const size_t upFrames = bridge.playback22050ToStereo44100(
        ramp, AUDIO_CAP_FRAMES, stereo, AUDIO_CAP_FRAMES * 2);
    assert(upFrames == AUDIO_CAP_FRAMES * 2);
    for (size_t i = 0; i < upFrames; ++i) assert(stereo[i * 2] == stereo[i * 2 + 1]);
    assert(stereo[1 * 2] == ramp[0]);
    assert(stereo[3 * 2] == ramp[1]);

    int16_t down[AUDIO_CAP_FRAMES * 2] = {};
    const size_t downFrames = bridge.captureStereo44100To22050(
        stereo, upFrames, down, AUDIO_CAP_FRAMES * 2);
    assert(downFrames >= AUDIO_CAP_FRAMES - 4 && downFrames <= AUDIO_CAP_FRAMES);
    bool nonSilent = false;
    for (size_t i = 0; i < downFrames; ++i) {
        if (down[i] != 0) nonSilent = true;
        assert(std::isfinite(static_cast<double>(down[i])));
    }
    assert(nonSilent);

    AudioCapPacket incoming = {};
    audioCapPacketInit(incoming, 10, AUDIO_CAP_PCM_VALID | AUDIO_CAP_BT_PAIRED,
                       ramp, AUDIO_CAP_FRAMES,
                       AUDIO_CAP_STATUS_READY | AUDIO_CAP_STATUS_BT_CONNECTED);
    assert(bridge.acceptCapPacket(incoming));
    assert(bridge.remoteStatus() & AUDIO_CAP_STATUS_BT_CONNECTED);
    int16_t captured[AUDIO_CAP_FRAMES] = {};
    assert(bridge.popCapture22050(captured, AUDIO_CAP_FRAMES) == AUDIO_CAP_FRAMES);
    assert(std::memcmp(captured, ramp, sizeof(ramp)) == 0);

    audioCapPacketInit(incoming, 12, AUDIO_CAP_PCM_VALID, ramp, 1);
    assert(bridge.acceptCapPacket(incoming));
    assert(bridge.stats().sequenceGaps == 1);
    incoming.crc32 ^= 1;
    assert(!bridge.acceptCapPacket(incoming));
    assert(bridge.stats().crcErrors == 1);

    bridge.reset();
    AudioCapPacket silence = {};
    bridge.buildHostPacket(silence);
    assert(silence.frames == 0 && (silence.flags & AUDIO_CAP_UNDERRUN));

    std::cout << "audio_cap_bridge_core: packet, ring and sample-rate tests passed\n";
    return 0;
}
