#include "ble_midi.h"

#include "ble_midi_codec.h"
#include "midi_input.h"
#include "pcm_ring.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <string.h>

namespace {
constexpr char kServiceUuid[] = "03B80E5A-EDE8-4B33-A751-6CE34EC4C700";
constexpr char kCharacteristicUuid[] = "7772E5DB-3868-4112-A1A9-F2669D106BF3";
constexpr uint8_t kMaximumPacketBytes = 64;

struct BlePacket {
    uint8_t length;
    uint8_t bytes[kMaximumPacketBytes];
};

SpscRing<BlePacket, 16> s_packets;
BleMidiDecoder s_decoder;
NimBLECharacteristic* s_characteristic = nullptr;
alignas(4) uint32_t s_available = 0;
alignas(4) uint32_t s_connected = 0;
alignas(4) uint32_t s_received = 0;
alignas(4) uint32_t s_sent = 0;
alignas(4) uint32_t s_malformed = 0;
alignas(4) uint32_t s_dropped = 0;

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*) override {
        __atomic_store_n(&s_connected, 1u, __ATOMIC_RELEASE);
    }
    void onDisconnect(NimBLEServer*) override {
        __atomic_store_n(&s_connected, 0u, __ATOMIC_RELEASE);
        NimBLEDevice::startAdvertising();
    }
};

class CharacteristicCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* characteristic) override {
        const std::string value = characteristic->getValue();
        if (value.empty() || value.size() > kMaximumPacketBytes) {
            __atomic_add_fetch(&s_malformed, 1u, __ATOMIC_RELAXED);
            return;
        }
        BlePacket packet = {};
        packet.length = static_cast<uint8_t>(value.size());
        memcpy(packet.bytes, value.data(), value.size());
        if (!s_packets.pushOne(packet))
            __atomic_add_fetch(&s_dropped, 1u, __ATOMIC_RELAXED);
        else
            __atomic_add_fetch(&s_received, 1u, __ATOMIC_RELAXED);
    }
};

ServerCallbacks s_serverCallbacks;
CharacteristicCallbacks s_characteristicCallbacks;
}  // namespace

void bleMidiInit() {
    s_packets.reset();
    s_decoder.reset();
    NimBLEDevice::init("Mini Studio 16");
    NimBLEServer* server = NimBLEDevice::createServer();
    if (!server) return;
    server->setCallbacks(&s_serverCallbacks);
    NimBLEService* service = server->createService(kServiceUuid);
    if (!service) return;
    s_characteristic = service->createCharacteristic(
        kCharacteristicUuid,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE |
        NIMBLE_PROPERTY::WRITE_NR | NIMBLE_PROPERTY::NOTIFY);
    if (!s_characteristic) return;
    s_characteristic->setCallbacks(&s_characteristicCallbacks);
    const uint8_t initial[] = {0x80, 0x80};
    s_characteristic->setValue(initial, sizeof(initial));
    service->start();
    NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
    advertising->addServiceUUID(kServiceUuid);
    advertising->setScanResponse(true);
    advertising->start();
    __atomic_store_n(&s_available, 1u, __ATOMIC_RELEASE);
}

void bleMidiUpdate() {
    BlePacket packet = {};
    uint8_t budget = 8;
    while (budget-- && s_packets.popOne(packet)) {
        const bool ok = s_decoder.decode(packet.bytes, packet.length,
            [](uint8_t byte) { midiInputFeedByte(byte); });
        if (!ok) __atomic_add_fetch(&s_malformed, 1u, __ATOMIC_RELAXED);
    }
}

bool bleMidiSend(const uint8_t* message, size_t length) {
    if (!s_characteristic ||
        __atomic_load_n(&s_connected, __ATOMIC_ACQUIRE) == 0 || length > 3)
        return false;
    uint8_t packet[5];
    const size_t encoded = bleMidiEncode(static_cast<uint16_t>(millis()), message,
                                         length, packet, sizeof(packet));
    if (!encoded) return false;
    s_characteristic->setValue(packet, encoded);
    s_characteristic->notify();
    __atomic_add_fetch(&s_sent, 1u, __ATOMIC_RELAXED);
    return true;
}

BleMidiSnapshot bleMidiSnapshot() {
    BleMidiSnapshot result = {};
    result.available = __atomic_load_n(&s_available, __ATOMIC_ACQUIRE) != 0;
    result.connected = __atomic_load_n(&s_connected, __ATOMIC_ACQUIRE) != 0;
    result.packetsReceived = __atomic_load_n(&s_received, __ATOMIC_RELAXED);
    result.packetsSent = __atomic_load_n(&s_sent, __ATOMIC_RELAXED);
    result.malformedPackets = __atomic_load_n(&s_malformed, __ATOMIC_RELAXED);
    result.droppedPackets = __atomic_load_n(&s_dropped, __ATOMIC_RELAXED);
    return result;
}
