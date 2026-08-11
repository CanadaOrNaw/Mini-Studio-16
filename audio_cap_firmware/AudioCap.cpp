#include "cap_config.h"

#include "../audio_cap_bridge_core.h"
#include "../audio_cap_protocol.h"

#include <Adafruit_NeoPixel.h>
#include <Arduino.h>
#include <BluetoothA2DPSource.h>
#include <driver/i2s.h>
#include <driver/spi_slave.h>

namespace {
AudioCapDeviceCore s_bridge;
AudioCapPlaybackUpsampler s_playbackConverter;
AudioCapCaptureResampler s_captureConverter;
BluetoothA2DPSource s_a2dp;
Adafruit_NeoPixel s_pixel(1, CAP_STATUS_RGB, NEO_GRB + NEO_KHZ800);
volatile bool s_connected = false;
volatile bool s_discovering = false;
volatile bool s_pairArmed = false;
volatile bool s_adcLocked = false;
volatile bool s_fault = false;

void showStatus() {
    uint32_t color = s_fault ? s_pixel.Color(40, 0, 0) :
        s_connected ? s_pixel.Color(0, 35, 4) :
        (s_pairArmed || s_discovering) ? s_pixel.Color(24, 0, 35) :
        s_adcLocked ? s_pixel.Color(0, 8, 30) : s_pixel.Color(20, 8, 0);
    s_pixel.setPixelColor(0, color);
    s_pixel.show();
}

void connectionChanged(esp_a2d_connection_state_t state, void*) {
    s_connected = state == ESP_A2D_CONNECTION_STATE_CONNECTED;
    showStatus();
}

void discoveryChanged(esp_bt_gap_discovery_state_t state) {
    s_discovering = state == ESP_BT_GAP_DISCOVERY_STARTED;
    showStatus();
}

bool selectDiscoveredSink(const char*, esp_bd_addr_t, int) {
    if (!s_pairArmed) return false;
    s_pairArmed = false;  // choose the first rendering device after Pair
    return true;
}

int32_t bluetoothPcm(uint8_t* bytes, int32_t byteCount) {
    if (!bytes || byteCount <= 0) return 0;
    int16_t* stereo = reinterpret_cast<int16_t*>(bytes);
    const size_t requestedFrames = static_cast<size_t>(byteCount) / 4u;
    size_t produced = 0;
    int16_t mono[128];
    while (produced < requestedFrames) {
        const size_t remaining = requestedFrames - produced;
        const size_t monoNeeded = (remaining + 1u) / 2u;
        const size_t chunk = monoNeeded > 128u ? 128u : monoNeeded;
        s_bridge.popPlayback(mono, chunk);  // zero-fills a short read
        produced += s_playbackConverter.process(
            mono, chunk, stereo + produced * 2u, requestedFrames - produced);
    }
    return static_cast<int32_t>(produced * 4u);
}

uint16_t deviceStatus() {
    uint16_t status = AUDIO_CAP_STATUS_PRESENT;
    if (s_connected) status |= AUDIO_CAP_STATUS_A2DP_CONNECTED;
    if (s_discovering || s_pairArmed) status |= AUDIO_CAP_STATUS_DISCOVERING;
    if (s_adcLocked) status |= AUDIO_CAP_STATUS_ADC_LOCKED;
    if (s_fault) status |= AUDIO_CAP_STATUS_FAULT;
    return status;
}

void processCommands(uint8_t commands) {
    if (commands & AUDIO_CAP_CMD_PAIR) {
        s_pairArmed = true;
        showStatus();
    }
    if (commands & AUDIO_CAP_CMD_DISCONNECT) {
        s_pairArmed = false;
        s_a2dp.disconnect();
    }
    if (commands & AUDIO_CAP_CMD_CLEAR) {
        // Keep live rings/converters intact; resetting them here would race the
        // I2S and A2DP tasks. The command clears only a latched fault indicator.
        s_fault = false;
        showStatus();
    }
}

void spiTask(void*) {
    spi_bus_config_t bus = {};
    bus.mosi_io_num = CAP_SPI_MOSI;
    bus.miso_io_num = CAP_SPI_MISO;
    bus.sclk_io_num = CAP_SPI_SCLK;
    bus.quadwp_io_num = -1;
    bus.quadhd_io_num = -1;
    bus.max_transfer_sz = sizeof(AudioCapPacket);
    spi_slave_interface_config_t slave = {};
    slave.spics_io_num = CAP_SPI_CS;
    slave.flags = 0;
    slave.queue_size = 1;
    slave.mode = 0;
    if (spi_slave_initialize(HSPI_HOST, &bus, &slave, 1) != ESP_OK) {
        s_fault = true;
        showStatus();
        vTaskDelete(nullptr);
    }

    static WORD_ALIGNED_ATTR AudioCapPacket receive;
    static WORD_ALIGNED_ATTR AudioCapPacket reply;
    digitalWrite(CAP_SPI_IRQ, HIGH);  // presence/wake; not a sample clock
    while (true) {
        s_bridge.buildReply(reply, deviceStatus());
        memset(&receive, 0, sizeof(receive));
        spi_slave_transaction_t transaction = {};
        transaction.length = sizeof(reply) * 8u;
        transaction.tx_buffer = &reply;
        transaction.rx_buffer = &receive;
        const esp_err_t result = spi_slave_transmit(HSPI_HOST, &transaction,
                                                    portMAX_DELAY);
        if (result != ESP_OK) {
            s_fault = true;
            showStatus();
            continue;
        }
        if (s_bridge.acceptTransfer(receive))
            processCommands(s_bridge.takeCommands());
    }
}

void adcTask(void*) {
    int32_t input[256 * 2];
    int16_t converted[128];
    while (true) {
        size_t bytesRead = 0;
        if (i2s_read(I2S_NUM_0, input, sizeof(input), &bytesRead,
                     pdMS_TO_TICKS(100)) != ESP_OK) {
            s_adcLocked = false;
            continue;
        }
        const size_t frames = bytesRead / (sizeof(int32_t) * 2u);
        const size_t outputFrames = s_captureConverter.process(
            input, frames, converted, sizeof(converted) / sizeof(converted[0]));
        if (outputFrames) {
            s_bridge.pushCapture(converted, outputFrames);
            s_adcLocked = true;
        }
    }
}

bool initAdc() {
    i2s_config_t config = {};
    config.mode = static_cast<i2s_mode_t>(I2S_MODE_SLAVE | I2S_MODE_RX);
    config.sample_rate = 48000;
    config.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT;
    config.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
    config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
    config.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
    config.dma_buf_count = 6;
    config.dma_buf_len = 256;
    config.use_apll = false;
    config.tx_desc_auto_clear = false;
    config.fixed_mclk = 0;
    i2s_pin_config_t pins = {};
    pins.bck_io_num = CAP_ADC_BCK;
    pins.ws_io_num = CAP_ADC_LRCK;
    pins.data_out_num = I2S_PIN_NO_CHANGE;
    pins.data_in_num = CAP_ADC_DOUT;
    return i2s_driver_install(I2S_NUM_0, &config, 0, nullptr) == ESP_OK &&
           i2s_set_pin(I2S_NUM_0, &pins) == ESP_OK;
}
}  // namespace

void setup() {
    Serial.begin(115200);
    pinMode(CAP_SPI_IRQ, OUTPUT);
    digitalWrite(CAP_SPI_IRQ, LOW);
    pinMode(CAP_PAIR_BUTTON, INPUT_PULLUP);
    s_pixel.begin();
    s_pixel.setBrightness(30);
    showStatus();

    if (!initAdc()) s_fault = true;
    xTaskCreatePinnedToCore(spiTask, "cap-spi", 4096, nullptr, 4, nullptr, 1);
    xTaskCreatePinnedToCore(adcTask, "cap-adc", 4096, nullptr, 3, nullptr, 1);

    s_a2dp.set_local_name("Mini Studio 16 Audio Cap");
    s_a2dp.set_auto_reconnect(true);
    s_a2dp.set_ssp_enabled(true);
    s_a2dp.set_ssid_callback(selectDiscoveredSink);
    s_a2dp.set_discovery_mode_callback(discoveryChanged);
    s_a2dp.set_on_connection_state_changed(connectionChanged, nullptr);
    s_a2dp.start_raw(bluetoothPcm);
    showStatus();
    Serial.println("MS16_AUDIO_CAP_READY protocol=1 adc=48000 bt=44100 host=22050");
}

void loop() {
    static bool previous = true;
    const bool pressed = digitalRead(CAP_PAIR_BUTTON) == LOW;
    if (pressed && previous) {
        s_pairArmed = true;
        showStatus();
    }
    previous = pressed;
    delay(20);
}
