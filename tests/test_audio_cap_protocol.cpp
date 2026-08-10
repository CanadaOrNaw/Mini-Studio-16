#include "../audio_cap_protocol.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    int16_t pcm[AUDIO_CAP_FRAMES];
    for (uint16_t index = 0; index < AUDIO_CAP_FRAMES; ++index)
        pcm[index] = static_cast<int16_t>(index * 127u - 8000);

    AudioCapPacket packet;
    audioCapPacketInit(packet, 0xFFFFFFFEu,
                       AUDIO_CAP_PCM_VALID | AUDIO_CAP_BT_PAIRED,
                       pcm, AUDIO_CAP_FRAMES);
    assert(audioCapPacketValidate(packet));
    assert(packet.sequence == 0xFFFFFFFEu && packet.frames == AUDIO_CAP_FRAMES);
    assert(std::memcmp(packet.pcm, pcm, sizeof(pcm)) == 0);

    AudioCapPacket corrupted = packet;
    corrupted.pcm[37] ^= 0x0100;
    assert(!audioCapPacketValidate(corrupted));
    corrupted = packet;
    corrupted.frames = AUDIO_CAP_FRAMES + 1;
    corrupted.crc32 = audioCapPacketCrc(corrupted);
    assert(!audioCapPacketValidate(corrupted));

    audioCapPacketInit(packet, 7, AUDIO_CAP_PCM_VALID, pcm, AUDIO_CAP_FRAMES + 40);
    assert(packet.frames == AUDIO_CAP_FRAMES && audioCapPacketValidate(packet));
    assert(audioCapSequenceFollows(0xFFFFFFFEu, 0xFFFFFFFFu));
    assert(audioCapSequenceFollows(0xFFFFFFFFu, 0u));
    assert(!audioCapSequenceFollows(4u, 6u));

    audioCapPacketInit(packet, 8, AUDIO_CAP_PCM_VALID, pcm, 4,
                       AUDIO_CAP_COMMAND_LINE_ENABLE);
    assert(packet.status == AUDIO_CAP_COMMAND_LINE_ENABLE);

    std::cout << "audio_cap_protocol: layout, CRC and bounds passed\n";
    return 0;
}
