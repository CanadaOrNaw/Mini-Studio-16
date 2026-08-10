#include "audio_cap_protocol.h"

#include <string.h>

namespace {
uint32_t updateCrc(uint32_t crc, const uint8_t* bytes, size_t length) {
    while (length--) {
        crc ^= *bytes++;
        for (uint8_t bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xEDB88320u &
                   static_cast<uint32_t>(-static_cast<int32_t>(crc & 1u)));
    }
    return crc;
}
}  // namespace

uint32_t audioCapPacketCrc(const AudioCapPacket& packet) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&packet);
    const size_t crcOffset = offsetof(AudioCapPacket, crc32);
    uint32_t crc = updateCrc(0xFFFFFFFFu, bytes, crcOffset);
    crc = updateCrc(crc, bytes + crcOffset + sizeof(packet.crc32),
                    sizeof(packet) - crcOffset - sizeof(packet.crc32));
    return crc ^ 0xFFFFFFFFu;
}

void audioCapPacketInit(AudioCapPacket& packet, uint32_t sequence, uint8_t flags,
                        const int16_t* pcm, uint16_t frames, uint16_t status) {
    memset(&packet, 0, sizeof(packet));
    packet.magic = AUDIO_CAP_MAGIC;
    packet.version = AUDIO_CAP_VERSION;
    packet.flags = flags;
    packet.sequence = sequence;
    packet.frames = frames <= AUDIO_CAP_FRAMES ? frames : AUDIO_CAP_FRAMES;
    packet.status = status;
    if (pcm && packet.frames)
        memcpy(packet.pcm, pcm, packet.frames * sizeof(packet.pcm[0]));
    packet.crc32 = audioCapPacketCrc(packet);
}

bool audioCapSequenceFollows(uint32_t previous, uint32_t current) {
    return current == previous + 1u;
}

bool audioCapPacketValidate(const AudioCapPacket& packet) {
    return packet.magic == AUDIO_CAP_MAGIC && packet.version == AUDIO_CAP_VERSION &&
           packet.frames <= AUDIO_CAP_FRAMES && packet.crc32 == audioCapPacketCrc(packet);
}
