#include "audio_cap.h"

#include "audio_cap_bridge_core.h"

#include <Arduino.h>
#include <SPI.h>

namespace {
constexpr int CAP_CS_PIN = 3;
constexpr int CAP_IRQ_PIN = 4;
constexpr int CAP_SCLK_PIN = 6;
constexpr int CAP_MOSI_PIN = 13;
constexpr int CAP_MISO_PIN = 5;
constexpr uint32_t CAP_SPI_HZ = 4000000;
constexpr uint32_t CAP_TRANSFER_US =
    static_cast<uint32_t>((static_cast<uint64_t>(AUDIO_CAP_FRAMES) * 1000000u) /
                          AUDIO_CAP_SAMPLE_RATE);

AudioCapHostCore s_core;
SPIClass s_spi(HSPI);
TaskHandle_t s_task = nullptr;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;
volatile uint8_t s_commands = 0;
volatile uint8_t s_monitor = 0;
volatile bool s_present = false;
volatile uint32_t s_transfers = 0;
volatile uint32_t s_transferErrors = 0;
AudioCapBridgeStats s_statsBaseline = {};
uint32_t s_transferBaseline = 0;
uint32_t s_errorBaseline = 0;

uint32_t delta(uint32_t value, uint32_t baseline) {
    return value - baseline;
}

bool transferOnce() {
    AudioCapPacket tx;
    AudioCapPacket rx;
    portENTER_CRITICAL(&s_mux);
    const uint8_t commands = s_commands;
    s_commands = 0;
    const uint8_t monitor = s_monitor;
    portEXIT_CRITICAL(&s_mux);
    s_core.buildTransfer(tx, commands, monitor);

    // P2-11 (reconciliation report): mode 1, matching the cap's DMA slave —
    // ESP-IDF requires modes 1/3 when the slave uses DMA (MISO timing).
    s_spi.beginTransaction(SPISettings(CAP_SPI_HZ, MSBFIRST, SPI_MODE1));
    digitalWrite(CAP_CS_PIN, LOW);
    s_spi.transferBytes(reinterpret_cast<uint8_t*>(&tx),
                        reinterpret_cast<uint8_t*>(&rx), sizeof(tx));
    digitalWrite(CAP_CS_PIN, HIGH);
    s_spi.endTransaction();

    const bool valid = s_core.acceptReply(rx);
    portENTER_CRITICAL(&s_mux);
    if (valid) ++s_transfers;
    else ++s_transferErrors;
    portEXIT_CRITICAL(&s_mux);
    return valid;
}

void capTask(void*) {
    uint32_t nextUs = micros();
    uint8_t failures = 0;
    while (true) {
        const bool irq = digitalRead(CAP_IRQ_PIN) == HIGH;
        if (!s_present && !irq) {
            vTaskDelay(pdMS_TO_TICKS(500));
            nextUs = micros();
            continue;
        }

        const uint32_t now = micros();
        if (static_cast<int32_t>(now - nextUs) < 0) {
            vTaskDelay(1);
            continue;
        }
        nextUs += CAP_TRANSFER_US;
        const bool valid = transferOnce();
        if (valid) {
            failures = 0;
            s_present = true;
        } else if (++failures >= 16) {
            failures = 0;
            s_present = false;
            s_core.reset();
            vTaskDelay(pdMS_TO_TICKS(500));
            nextUs = micros();
        }
    }
}
}  // namespace

void audioCapInit() {
    pinMode(CAP_CS_PIN, OUTPUT);
    digitalWrite(CAP_CS_PIN, HIGH);
    pinMode(CAP_IRQ_PIN, INPUT_PULLDOWN);
    s_spi.begin(CAP_SCLK_PIN, CAP_MISO_PIN, CAP_MOSI_PIN, CAP_CS_PIN);
    xTaskCreatePinnedToCore(capTask, "audio-cap", 4096, nullptr, 1, &s_task, 1);
}

void audioCapProcessAudioBlock(int16_t* masterPcm, size_t frames) {
    if (!masterPcm || frames == 0) return;
    if (!s_present) return;
    s_core.pushPlayback(masterPcm, frames);
    int16_t line[256];
    size_t offset = 0;
    while (offset < frames) {
        const size_t chunk = frames - offset > 256 ? 256 : frames - offset;
        s_core.popCapture(line, chunk);
        const int32_t monitor = s_monitor;
        for (size_t index = 0; index < chunk; ++index) {
            int32_t mixed = masterPcm[offset + index] +
                (static_cast<int32_t>(line[index]) * monitor) / 100;
            if (mixed > 32767) mixed = 32767;
            else if (mixed < -32768) mixed = -32768;
            masterPcm[offset + index] = static_cast<int16_t>(mixed);
        }
        offset += chunk;
    }
}

AudioCapSnapshot audioCapSnapshot() {
    const AudioCapBridgeStats& stats = s_core.stats();
    const uint16_t remote = s_core.remoteStatus();
    AudioCapSnapshot result = {};
    result.present = s_present && (remote & AUDIO_CAP_STATUS_PRESENT);
    result.a2dpConnected = remote & AUDIO_CAP_STATUS_A2DP_CONNECTED;
    result.discovering = remote & AUDIO_CAP_STATUS_DISCOVERING;
    result.adcLocked = remote & AUDIO_CAP_STATUS_ADC_LOCKED;
    result.fault = remote & AUDIO_CAP_STATUS_FAULT;
    result.monitorPercent = s_monitor;
    result.transfers = delta(s_transfers, s_transferBaseline);
    result.transferErrors = delta(s_transferErrors, s_errorBaseline);
    result.playbackDrops = delta(stats.playbackDrops, s_statsBaseline.playbackDrops);
    result.captureDrops = delta(stats.captureDrops, s_statsBaseline.captureDrops);
    result.playbackUnderruns = delta(stats.playbackUnderruns,
                                     s_statsBaseline.playbackUnderruns);
    result.captureUnderruns = delta(stats.captureUnderruns,
                                    s_statsBaseline.captureUnderruns);
    result.sequenceGaps = delta(stats.sequenceGaps, s_statsBaseline.sequenceGaps);
    return result;
}

void audioCapRequestPair() {
    portENTER_CRITICAL(&s_mux);
    s_commands |= AUDIO_CAP_CMD_PAIR;
    portEXIT_CRITICAL(&s_mux);
}

void audioCapRequestDisconnect() {
    portENTER_CRITICAL(&s_mux);
    s_commands |= AUDIO_CAP_CMD_DISCONNECT;
    portEXIT_CRITICAL(&s_mux);
}

void audioCapClearStats() {
    portENTER_CRITICAL(&s_mux);
    s_commands |= AUDIO_CAP_CMD_CLEAR;
    s_statsBaseline = s_core.stats();
    s_transferBaseline = s_transfers;
    s_errorBaseline = s_transferErrors;
    portEXIT_CRITICAL(&s_mux);
}

void audioCapSetMonitor(uint8_t percent) {
    portENTER_CRITICAL(&s_mux);
    s_monitor = percent <= 100 ? percent : 100;
    portEXIT_CRITICAL(&s_mux);
}
