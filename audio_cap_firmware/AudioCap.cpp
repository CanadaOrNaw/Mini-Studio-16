#include <Arduino.h>

#include "../audio_cap_bridge_core.h"
#include "../audio_cap_protocol.h"
#include "../pcm_ring.h"
#include "cap_config.h"

#include <driver/i2s.h>
#include <driver/spi_slave.h>
#include <esp_a2dp_api.h>
#include <esp_bt.h>
#include <esp_bt_device.h>
#include <esp_bt_main.h>
#include <esp_gap_bt_api.h>
#include <nvs_flash.h>

#include <string.h>

namespace {
DMA_ATTR AudioCapPacket s_hostPacket = {};
DMA_ATTR AudioCapPacket s_capPacket = {};
DMA_ATTR int16_t s_i2sInput[CAP_I2S_DMA_FRAMES * 2] = {};
DMA_ATTR int16_t s_stereoOutput[AUDIO_CAP_FRAMES * 4] = {};

SpscRing<int16_t, 8192> s_a2dpSamples;  // interleaved stereo samples
SpscRing<int16_t, 2048> s_lineSamples;  // 22.05 kHz mono frames
AudioCapBridgeCore s_playbackRate;
AudioCapBridgeCore s_captureRate;

volatile bool s_btConnected = false;
volatile bool s_btDiscovering = false;
volatile bool s_lineEnabled = false;
volatile bool s_haveCandidate = false;
esp_bd_addr_t s_candidate = {};
uint32_t s_txSequence = 0;
uint32_t s_lastHostSequence = 0;
bool s_haveHostSequence = false;
uint32_t s_spiUnderruns = 0;
uint32_t s_spiOverruns = 0;
uint32_t s_crcErrors = 0;

bool isAudioRenderer(uint32_t cod) {
    return esp_bt_gap_is_valid_cod(cod) &&
           (esp_bt_gap_get_cod_srvc(cod) & ESP_BT_COD_SRVC_RENDERING) != 0;
}

void startDiscovery() {
    if (s_btConnected) return;
    s_haveCandidate = false;
    s_btDiscovering = true;
    esp_bt_gap_cancel_discovery();
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);
}

void gapCallback(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* parameter) {
    if (!parameter) return;
    if (event == ESP_BT_GAP_DISC_RES_EVT && !s_haveCandidate) {
        uint32_t cod = 0;
        for (int index = 0; index < parameter->disc_res.num_prop; ++index) {
            const esp_bt_gap_dev_prop_t& property = parameter->disc_res.prop[index];
            if (property.type == ESP_BT_GAP_DEV_PROP_COD && property.val &&
                property.len >= static_cast<int>(sizeof(cod))) {
                memcpy(&cod, property.val, sizeof(cod));
            }
        }
        if (isAudioRenderer(cod)) {
            memcpy(s_candidate, parameter->disc_res.bda, sizeof(esp_bd_addr_t));
            s_haveCandidate = true;
            esp_bt_gap_cancel_discovery();
        }
    } else if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT &&
               parameter->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STOPPED) {
        s_btDiscovering = false;
        if (s_haveCandidate) esp_a2d_source_connect(s_candidate);
    }
}

void a2dpCallback(esp_a2d_cb_event_t event, esp_a2d_cb_param_t* parameter) {
    if (!parameter) return;
    if (event == ESP_A2D_CONNECTION_STATE_EVT) {
        s_btConnected = parameter->conn_stat.state == ESP_A2D_CONNECTION_STATE_CONNECTED;
        digitalWrite(CAP_STATUS_LED, s_btConnected ? HIGH : LOW);
    }
}

int32_t a2dpData(uint8_t* destination, int32_t length) {
    if (!destination || length <= 0) return 0;
    const size_t samplesRequested = static_cast<size_t>(length) / sizeof(int16_t);
    const size_t samplesRead = s_a2dpSamples.pop(
        reinterpret_cast<int16_t*>(destination), samplesRequested);
    if (samplesRead < samplesRequested) {
        memset(destination + samplesRead * sizeof(int16_t), 0,
               (samplesRequested - samplesRead) * sizeof(int16_t));
    }
    return length;
}

void initBluetooth() {
    esp_bt_controller_mem_release(ESP_BT_MODE_BLE);
    esp_bt_controller_config_t controller = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_bt_controller_init(&controller));
    ESP_ERROR_CHECK(esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT));
    ESP_ERROR_CHECK(esp_bluedroid_init());
    ESP_ERROR_CHECK(esp_bluedroid_enable());
    ESP_ERROR_CHECK(esp_bt_dev_set_device_name("Mini Studio Audio Cap"));
    ESP_ERROR_CHECK(esp_bt_gap_register_callback(gapCallback));
    ESP_ERROR_CHECK(esp_a2d_register_callback(a2dpCallback));
    ESP_ERROR_CHECK(esp_a2d_source_register_data_callback(a2dpData));
    ESP_ERROR_CHECK(esp_a2d_source_init());
    esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
}

void initLineAdc() {
    i2s_config_t config = {};
    // PCM1808 is the clock master from the low-jitter 11.2896 MHz oscillator;
    // the ESP32 must receive its BCK/LRCK rather than creating a second clock.
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_SLAVE | I2S_MODE_RX);
    config.sample_rate = CAP_A2DP_RATE;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 4;
    config.dma_buf_len = CAP_I2S_DMA_FRAMES;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;
    i2s_pin_config_t pins = {};
    pins.bck_io_num = CAP_I2S_BCLK;
    pins.ws_io_num = CAP_I2S_LRCK;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = CAP_I2S_ADC_DATA;
    ESP_ERROR_CHECK(i2s_driver_install(I2S_NUM_1, &config, 0, nullptr));
    ESP_ERROR_CHECK(i2s_set_pin(I2S_NUM_1, &pins));
    ESP_ERROR_CHECK(i2s_zero_dma_buffer(I2S_NUM_1));
}

uint16_t capStatus() {
    uint16_t status = AUDIO_CAP_STATUS_READY | AUDIO_CAP_STATUS_ADC_LOCKED;
    if (s_btDiscovering) status |= AUDIO_CAP_STATUS_BT_DISCOVERING;
    if (s_btConnected) status |= AUDIO_CAP_STATUS_BT_CONNECTED;
    if (s_lineEnabled) status |= AUDIO_CAP_STATUS_LINE_ACTIVE;
    return status;
}

void applyHostCommands(uint16_t commands) {
    if (commands & AUDIO_CAP_COMMAND_PAIR) startDiscovery();
    if (commands & AUDIO_CAP_COMMAND_DISCONNECT) {
        if (s_btConnected) esp_a2d_source_disconnect(s_candidate);
    }
    if (commands & AUDIO_CAP_COMMAND_LINE_ENABLE) s_lineEnabled = true;
    if (commands & AUDIO_CAP_COMMAND_LINE_DISABLE) s_lineEnabled = false;
    if (commands & AUDIO_CAP_COMMAND_CLEAR_STATS) {
        s_spiUnderruns = 0;
        s_spiOverruns = 0;
        s_crcErrors = 0;
    }
}

void lineTask(void*) {
    while (true) {
        size_t bytesRead = 0;
        if (i2s_read(I2S_NUM_1, s_i2sInput, sizeof(s_i2sInput), &bytesRead,
                     portMAX_DELAY) != ESP_OK) continue;
        const size_t frames = bytesRead / (sizeof(int16_t) * 2u);
        int16_t mono[CAP_I2S_DMA_FRAMES / 2 + 4] = {};
        const size_t produced = s_captureRate.captureStereo44100To22050(
            s_i2sInput, frames, mono, sizeof(mono) / sizeof(mono[0]));
        const size_t pushed = s_lineSamples.push(mono, produced);
        if (pushed != produced) s_spiOverruns += produced - pushed;
    }
}

void spiTask(void*) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = CAP_SPI_MOSI;
    bus.miso_io_num = CAP_SPI_MISO;
    bus.sclk_io_num = CAP_SPI_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    spi_slave_interface_config_t slave = {};
    slave.spics_io_num = CAP_SPI_CS;
    slave.flags = 0;
    slave.queue_size = 2;
    slave.mode = 0;
    ESP_ERROR_CHECK(spi_slave_initialize(HSPI_HOST, &bus, &slave, SPI_DMA_CH_AUTO));

    while (true) {
        int16_t line[AUDIO_CAP_FRAMES] = {};
        const size_t lineFrames = s_lineEnabled
            ? s_lineSamples.pop(line, AUDIO_CAP_FRAMES) : 0;
        uint8_t flags = lineFrames ? AUDIO_CAP_PCM_VALID | AUDIO_CAP_LINE_SELECTED : 0;
        if (lineFrames < AUDIO_CAP_FRAMES && s_lineEnabled) flags |= AUDIO_CAP_UNDERRUN;
        if (s_btConnected) flags |= AUDIO_CAP_BT_PAIRED;
        if (s_spiOverruns) flags |= AUDIO_CAP_OVERRUN;
        audioCapPacketInit(s_capPacket, s_txSequence++, flags, line,
                           static_cast<uint16_t>(lineFrames), capStatus());

        spi_slave_transaction_t transaction = {};
        transaction.length = sizeof(AudioCapPacket) * 8u;
        transaction.tx_buffer = &s_capPacket;
        transaction.rx_buffer = &s_hostPacket;
        digitalWrite(CAP_HOST_IRQ, HIGH);
        const esp_err_t result = spi_slave_transmit(HSPI_HOST, &transaction, portMAX_DELAY);
        digitalWrite(CAP_HOST_IRQ, LOW);
        if (result != ESP_OK || transaction.trans_len != sizeof(AudioCapPacket) * 8u)
            continue;
        if (!audioCapPacketValidate(s_hostPacket)) {
            ++s_crcErrors;
            continue;
        }
        if (s_haveHostSequence &&
            !audioCapSequenceFollows(s_lastHostSequence, s_hostPacket.sequence))
            ++s_spiUnderruns;
        s_lastHostSequence = s_hostPacket.sequence;
        s_haveHostSequence = true;
        applyHostCommands(s_hostPacket.status);
        if ((s_hostPacket.flags & AUDIO_CAP_PCM_VALID) && s_hostPacket.frames) {
            int16_t aligned[AUDIO_CAP_FRAMES] = {};
            memcpy(aligned, s_hostPacket.pcm,
                   s_hostPacket.frames * sizeof(aligned[0]));
            const size_t stereoFrames = s_playbackRate.playback22050ToStereo44100(
                aligned, s_hostPacket.frames, s_stereoOutput,
                AUDIO_CAP_FRAMES * 2u);
            const size_t stereoSamples = stereoFrames * 2u;
            const size_t pushed = s_a2dpSamples.push(s_stereoOutput, stereoSamples);
            if (pushed != stereoSamples) s_spiOverruns += stereoSamples - pushed;
        }
    }
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(CAP_HOST_IRQ, OUTPUT);
    pinMode(CAP_PAIR_BUTTON, INPUT_PULLUP);
    pinMode(CAP_STATUS_LED, OUTPUT);
    digitalWrite(CAP_HOST_IRQ, LOW);
    digitalWrite(CAP_STATUS_LED, LOW);
    ESP_ERROR_CHECK(nvs_flash_init());
    initLineAdc();
    initBluetooth();
    xTaskCreatePinnedToCore(lineTask, "line-adc", 4096, nullptr, 2, nullptr, 0);
    xTaskCreatePinnedToCore(spiTask, "host-spi", 6144, nullptr, 3, nullptr, 1);
    Serial.println("MS16-CAP/1 READY rev=A status=not-hardware-verified");
}

void loop() {
    static bool previousButton = true;
    const bool button = digitalRead(CAP_PAIR_BUTTON) != 0;
    if (previousButton && !button) startDiscovery();
    previousButton = button;

    static uint32_t lastReport = 0;
    if (millis() - lastReport >= 5000u) {
        lastReport = millis();
        Serial.printf("MS16-CAP/1 STATUS bt=%u discover=%u line=%u a2dp=%lu lineFrames=%lu "
                      "crc=%lu gaps=%lu overruns=%lu\n",
                      s_btConnected ? 1u : 0u, s_btDiscovering ? 1u : 0u,
                      s_lineEnabled ? 1u : 0u,
                      static_cast<unsigned long>(s_a2dpSamples.size() / 2u),
                      static_cast<unsigned long>(s_lineSamples.size()),
                      static_cast<unsigned long>(s_crcErrors),
                      static_cast<unsigned long>(s_spiUnderruns),
                      static_cast<unsigned long>(s_spiOverruns));
    }
    delay(10);
}
