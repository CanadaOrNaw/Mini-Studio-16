#pragma once

#include <stddef.h>
#include <stdint.h>

constexpr uint16_t AUDIO_CAP_MAGIC = 0x534Du;  // little-endian "MS"
constexpr uint8_t AUDIO_CAP_VERSION = 1;
constexpr uint16_t AUDIO_CAP_FRAMES = 128;
constexpr uint32_t AUDIO_CAP_SAMPLE_RATE = 22050;

enum AudioCapFlags : uint8_t {
    AUDIO_CAP_PCM_VALID = 1u << 0,
    AUDIO_CAP_LINE_SELECTED = 1u << 1,
    AUDIO_CAP_BT_PAIRED = 1u << 2,
    AUDIO_CAP_UNDERRUN = 1u << 3,
    AUDIO_CAP_OVERRUN = 1u << 4,
};

// The status word is deliberately split in half so a single SPI transaction
// can carry host commands and cap state without changing the v1 wire layout.
// Host commands are edge-triggered; the cap acknowledges the latest command
// sequence by returning AUDIO_CAP_STATUS_COMMAND_ACK.
enum AudioCapCommand : uint16_t {
    AUDIO_CAP_COMMAND_NONE = 0,
    AUDIO_CAP_COMMAND_PAIR = 1u << 0,
    AUDIO_CAP_COMMAND_DISCONNECT = 1u << 1,
    AUDIO_CAP_COMMAND_LINE_ENABLE = 1u << 2,
    AUDIO_CAP_COMMAND_LINE_DISABLE = 1u << 3,
    AUDIO_CAP_COMMAND_CLEAR_STATS = 1u << 4,
};

enum AudioCapStatus : uint16_t {
    AUDIO_CAP_STATUS_READY = 1u << 8,
    AUDIO_CAP_STATUS_ADC_LOCKED = 1u << 9,
    AUDIO_CAP_STATUS_BT_DISCOVERING = 1u << 10,
    AUDIO_CAP_STATUS_BT_CONNECTED = 1u << 11,
    AUDIO_CAP_STATUS_LINE_ACTIVE = 1u << 12,
    AUDIO_CAP_STATUS_COMMAND_ACK = 1u << 13,
    AUDIO_CAP_STATUS_FAULT = 1u << 15,
};

// Fixed-size full-duplex SPI packet. Host->cap PCM is the master mix; cap->host
// PCM is line/Bluetooth input. At 128 frames it represents ~5.8 ms and only
// ~47 KiB/s in each direction before SPI framing.
struct __attribute__((packed)) AudioCapPacket {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint32_t sequence;
    uint16_t frames;
    uint16_t status;
    uint32_t crc32;
    int16_t pcm[AUDIO_CAP_FRAMES];
};

static_assert(sizeof(AudioCapPacket) == 272, "audio cap wire layout changed");

void audioCapPacketInit(AudioCapPacket& packet, uint32_t sequence, uint8_t flags,
                        const int16_t* pcm, uint16_t frames, uint16_t status = 0);
uint32_t audioCapPacketCrc(const AudioCapPacket& packet);
bool audioCapPacketValidate(const AudioCapPacket& packet);
bool audioCapSequenceFollows(uint32_t previous, uint32_t current);
