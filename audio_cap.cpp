#include "audio_cap.h"

#include "audio_cap_bridge_core.h"
#include "config.h"

#include <string.h>

#if defined(ESP32)
#include <Arduino.h>
#include <SPI.h>

namespace {
AudioCapBridgeCore s_bridge;
SPIClass s_spi(HSPI);
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
AudioCapSnapshot s_snapshot = {};
volatile uint16_t s_pendingCommands = 0;
volatile uint8_t s_monitorPercent = 0;
volatile bool s_detected = false;

int16_t mixPcm(int16_t dry, int16_t input, uint8_t percent) {
    int32_t mixed = static_cast<int32_t>(dry) +
                    static_cast<int32_t>(input) * percent / 100;
    if (mixed > 32767) mixed = 32767;
    if (mixed < -32768) mixed = -32768;
    return static_cast<int16_t>(mixed);
}

void capTask(void*) {
    AudioCapPacket tx = {};
    AudioCapPacket rx = {};
    uint32_t lastTransferUs = 0;
    while (true) {
        const uint32_t now = micros();
        const bool requested = digitalRead(AUDIO_CAP_IRQ_PIN) != 0;
        const bool active = __atomic_load_n(&s_detected, __ATOMIC_ACQUIRE);
        // An absent optional cap must cost almost nothing. Poll twice per
        // second until its IRQ appears, then service the 5.8 ms audio cadence.
        const uint32_t intervalUs = active ? 4500u : 500000u;
        if (!requested && now - lastTransferUs < intervalUs) {
            vTaskDelay(1);
            continue;
        }
        lastTransferUs = now;
        const uint16_t commands = __atomic_exchange_n(&s_pendingCommands, 0u,
                                                       __ATOMIC_ACQ_REL);
        s_bridge.buildHostPacket(tx, commands);
        memset(&rx, 0, sizeof(rx));
        s_spi.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
        digitalWrite(AUDIO_CAP_CS_PIN, LOW);
        s_spi.transferBytes(reinterpret_cast<uint8_t*>(&tx),
                            reinterpret_cast<uint8_t*>(&rx), sizeof(tx));
        digitalWrite(AUDIO_CAP_CS_PIN, HIGH);
        s_spi.endTransaction();
        const bool accepted = s_bridge.acceptCapPacket(rx);
        const AudioCapBridgeStats stats = s_bridge.stats();
        portENTER_CRITICAL(&s_mux);
        ++s_snapshot.transfers;
        const bool detected = accepted &&
            ((s_bridge.remoteStatus() & AUDIO_CAP_STATUS_READY) != 0);
        s_snapshot.detected = detected;
        __atomic_store_n(&s_detected, detected, __ATOMIC_RELEASE);
        s_snapshot.status = s_bridge.remoteStatus();
        s_snapshot.bluetoothConnected =
            (s_bridge.remoteStatus() & AUDIO_CAP_STATUS_BT_CONNECTED) != 0;
        s_snapshot.crcErrors = stats.crcErrors;
        s_snapshot.sequenceGaps = stats.sequenceGaps;
        if (s_bridge.remoteFlags() & AUDIO_CAP_UNDERRUN) ++s_snapshot.capUnderruns;
        if (s_bridge.remoteFlags() & AUDIO_CAP_OVERRUN) ++s_snapshot.capOverruns;
        portEXIT_CRITICAL(&s_mux);
    }
}
}  // namespace

void audioCapInit() {
    pinMode(AUDIO_CAP_CS_PIN, OUTPUT);
    pinMode(AUDIO_CAP_IRQ_PIN, INPUT_PULLDOWN);
    pinMode(AUDIO_CAP_RESET_PIN, OUTPUT);
    digitalWrite(AUDIO_CAP_CS_PIN, HIGH);
    digitalWrite(AUDIO_CAP_RESET_PIN, LOW);
    s_spi.begin(AUDIO_CAP_SCLK_PIN, AUDIO_CAP_MISO_PIN, AUDIO_CAP_MOSI_PIN,
                AUDIO_CAP_CS_PIN);
    delay(5);
    digitalWrite(AUDIO_CAP_RESET_PIN, HIGH);
    portENTER_CRITICAL(&s_mux);
    s_snapshot.initialized = true;
    portEXIT_CRITICAL(&s_mux);
    xTaskCreatePinnedToCore(capTask, "audio-cap", 4096, nullptr, 1, &s_task, 1);
}

void audioCapProcessAudioBlock(int16_t* master, size_t frames) {
    if (!master || frames == 0) return;
    if (!__atomic_load_n(&s_detected, __ATOMIC_ACQUIRE)) return;
    const size_t pushed = s_bridge.pushPlayback22050(master, frames);
    if (pushed != frames) {
        portENTER_CRITICAL(&s_mux);
        s_snapshot.playbackDrops += static_cast<uint32_t>(frames - pushed);
        portEXIT_CRITICAL(&s_mux);
    }
    int16_t capture[AUDIO_BUF_LEN];
    if (frames > AUDIO_BUF_LEN) frames = AUDIO_BUF_LEN;
    const size_t popped = s_bridge.popCapture22050(capture, frames);
    const uint8_t monitor = __atomic_load_n(&s_monitorPercent, __ATOMIC_ACQUIRE);
    if (monitor) {
        for (size_t index = 0; index < popped; ++index)
            master[index] = mixPcm(master[index], capture[index], monitor);
    }
    if (popped < frames) {
        portENTER_CRITICAL(&s_mux);
        s_snapshot.captureUnderruns += static_cast<uint32_t>(frames - popped);
        portEXIT_CRITICAL(&s_mux);
    }
}

AudioCapSnapshot audioCapSnapshot() {
    portENTER_CRITICAL(&s_mux);
    AudioCapSnapshot result = s_snapshot;
    portEXIT_CRITICAL(&s_mux);
    result.monitorPercent = __atomic_load_n(&s_monitorPercent, __ATOMIC_ACQUIRE);
    result.lineMonitor = result.monitorPercent != 0;
    return result;
}

void audioCapSetMonitor(uint8_t percent) {
    if (percent > 100) percent = 100;
    __atomic_store_n(&s_monitorPercent, percent, __ATOMIC_RELEASE);
    __atomic_fetch_or(&s_pendingCommands,
                      percent ? AUDIO_CAP_COMMAND_LINE_ENABLE
                              : AUDIO_CAP_COMMAND_LINE_DISABLE,
                      __ATOMIC_ACQ_REL);
}
void audioCapRequestPair() {
    __atomic_fetch_or(&s_pendingCommands, AUDIO_CAP_COMMAND_PAIR, __ATOMIC_ACQ_REL);
}
void audioCapRequestDisconnect() {
    __atomic_fetch_or(&s_pendingCommands, AUDIO_CAP_COMMAND_DISCONNECT,
                      __ATOMIC_ACQ_REL);
}
void audioCapRequestClearStats() {
    __atomic_fetch_or(&s_pendingCommands, AUDIO_CAP_COMMAND_CLEAR_STATS,
                      __ATOMIC_ACQ_REL);
    portENTER_CRITICAL(&s_mux);
    const bool initialized = s_snapshot.initialized;
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_snapshot.initialized = initialized;
    portEXIT_CRITICAL(&s_mux);
}

#else
void audioCapInit() {}
void audioCapProcessAudioBlock(int16_t*, size_t) {}
AudioCapSnapshot audioCapSnapshot() { return AudioCapSnapshot(); }
void audioCapSetMonitor(uint8_t) {}
void audioCapRequestPair() {}
void audioCapRequestDisconnect() {}
void audioCapRequestClearStats() {}
#endif
