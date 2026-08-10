// ============================================================
// Microgroove — a pocket groovebox for the M5Stack Cardputer-ADV
// by lebiro.studio
//   - 3 synth tracks (saw/sqr/tri/sin + wavetables, 303-style resonant
//     filter, accent, slide); each track switchable 1-3 voices
//     (mono 303 or polyphonic chords)
//   - 8 drum lanes: 808 synth / 909 synth / SD samples, choke groups
//   - 16 patterns x 16 steps, 128-entry chain, live record with quantize
//   - live mic sampling, SD-streamed sampler/looper, master/stem recording
//   - BLE/USB MIDI, BMI270 motion, and USB serial control
//   - project save/load to microSD (GBX v7; loads v1-v6 transparently)
//
// Portions of the synth voice, 808 drums, and audio task are derived
// from qwertyuu/Cardputer-Adv-Tracker (MIT License) - see LICENSE.
// ============================================================
#include <M5Cardputer.h>
#include <SPI.h>
#include <SD.h>
#include <esp_heap_caps.h>

#include "config.h"
#include "sequencer.h"
#include "sampler.h"
#include "wavetable.h"
#include "storage.h"
#include "audio_engine.h"
#include "mic_sampler.h"
#include "sd_diagnostics.h"
#include "master_recorder.h"
#include "serial_control.h"
#include "midi_input.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "streaming_sampler.h"
#include "motion.h"
#include "ble_midi.h"
#include "usb_midi.h"
#include "sd_io_arbiter.h"
#include "ui.h"

void inputInit();
void inputUpdate();

static bool s_sdOk = false;

void setup() {
    Serial.begin(115200);
    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    uiInit();

    // Speaker / codec
    auto spk = M5Cardputer.Speaker.config();
    spk.sample_rate   = SAMPLE_RATE;
    spk.task_priority = 3;
    spk.dma_buf_count = 4;
    spk.dma_buf_len   = AUDIO_BUF_LEN;
    M5Cardputer.Speaker.config(spk);
    M5Cardputer.Speaker.begin();
    M5Cardputer.Speaker.setVolume(200);

    // SD (Cardputer-ADV pinout)
    SPI.begin(SD_SPI_CLK_PIN, SD_SPI_MISO_PIN, SD_SPI_MOSI_PIN, SD_SPI_CS_PIN);
    s_sdOk = SD.begin(SD_SPI_CS_PIN, SPI, 25000000);
    sdIoInit();
    sdDiagnosticsInit(s_sdOk);
    masterRecorderInit(s_sdOk);
    stemRecorderInit(s_sdOk);

    // Modules
    const bool legacyCaptureOk = micSamplerInit();
    bool legacySamplerOk = false;
    wavetableInitBuiltins();
    if (s_sdOk) {
        legacySamplerOk = samplerInit();  // also creates /groovebox dirs
        wavetableLoadUserFromSD();
    }
    loopEngineInit(s_sdOk);
    streamingSamplerInit(s_sdOk);
    sequencerInit();
    midiInputInit();
    motionInit();
    bleMidiInit();
    usbMidiInit();
    loadDemoPattern();

    uiSplash();
    while (true) {
        M5Cardputer.update();
        if (M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed()) break;
        if (M5Cardputer.BtnA.wasPressed()) break;
        delay(30);
    }

    inputInit();
    audioEngineStart();              // render task on core 0
    serialControlInit();

    const LoopEngineSnapshot bootLoops = loopEngineSnapshot();
    const StreamingSamplerSnapshot bootSampler = streamingSamplerSnapshot();
    const MotionSnapshot bootMotion = motionSnapshot();
    const BleMidiSnapshot bootBle = bleMidiSnapshot();
    const UsbMidiSnapshot bootUsb = usbMidiSnapshot();
    Serial.printf("BOOT_READY sd=%u heapFree=%lu heapLargest=%lu\n",
                  s_sdOk ? 1u : 0u,
                  static_cast<unsigned long>(heap_caps_get_free_size(
                      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned long>(heap_caps_get_largest_free_block(
                      MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)));
    Serial.printf("BOOT_SUBSYSTEM legacyCapture=%u legacySampler=%u loop=%u streamSampler=%u "
                  "motion=%u ble=%u usb=%u usbHost=%u\n",
                  legacyCaptureOk ? 1u : 0u, legacySamplerOk ? 1u : 0u,
                  bootLoops.available ? 1u : 0u, bootSampler.available ? 1u : 0u,
                  bootMotion.available ? 1u : 0u, bootBle.available ? 1u : 0u,
                  bootUsb.available ? 1u : 0u, bootUsb.hostMode ? 1u : 0u);

    if (!s_sdOk) uiStatus("NO SD CARD");
    g_needRedraw = true;
}

void loop() {
    serialControlUpdate();
    bleMidiUpdate();
    usbMidiUpdate();
    midiInputUpdate();
    motionUpdate();
    inputUpdate();
    micSamplerUpdate();
    sequencerTick();

    if (g_needRedraw) {
        uiDraw();
        g_needRedraw = false;
    }

    if (g_curPage == PAGE_DIAG && sdDiagnosticsIsRunning()) {
        static uint32_t lastDiagDraw = 0;
        if (millis() - lastDiagDraw > 100) {
            lastDiagDraw = millis();
            g_needRedraw = true;
        }
    }

    // keep the scope alive on the sound page
    if (g_curPage == PAGE_SOUND && g_playing) {
        static uint32_t last = 0;
        if (millis() - last > 60) { last = millis(); uiDraw(); }
    }

    delay(4);
}
