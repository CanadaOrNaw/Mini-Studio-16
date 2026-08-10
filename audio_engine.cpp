// ============================================================
// CardputerGroovebox - audio_engine.cpp
// Render task derived from qwertyuu/Cardputer-Adv-Tracker (MIT),
// extended with 8 drum lanes + sample voices.
// ============================================================
#include "audio_engine.h"
#include "sequencer.h"
#include "sampler.h"
#include <M5Cardputer.h>
#include "mic_sampler.h"
#include "master_recorder.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "streaming_sampler.h"

float g_scopeBuf[SCREEN_W];
volatile int g_scopeIdx = 0;

static int16_t s_bufA[AUDIO_BUF_LEN];
static int16_t s_bufB[AUDIO_BUF_LEN];
static StemPcmFrame s_stemBuf[AUDIO_BUF_LEN];
static TaskHandle_t s_task = nullptr;

static int16_t toPcm(float sample) {
    if (sample > 1.0f) sample = 1.0f;
    else if (sample < -1.0f) sample = -1.0f;
    return static_cast<int16_t>(sample * 12000.0f);
}

static void audioTask(void*) {
    int16_t* buffers[2] = { s_bufA, s_bufB };
    int cur = 0;

    while (true) {
        int16_t* buf = buffers[cur];

        for (int i = 0; i < AUDIO_BUF_LEN; i++) {
            float synthBus[NUM_SYNTHS] = {0.0f, 0.0f, 0.0f};
            float drumBus = 0.0f;

            for (int s = 0; s < NUM_SYNTHS; s++)
                if (!g_synthMute[s]) synthBus[s] = g_synths[s].render();

            if (!g_drumMute)
                for (int d = 0; d < NUM_DRUM_LANES; d++)
                    drumBus += g_drumLanes[d].render() * 0.6f;

            const int32_t streamedSamplePcm = streamingSamplerRender();
            const float sampleBus = g_previewVoice.render() +
                static_cast<float>(streamedSamplePcm) / 32768.0f;
            const float dryMix = synthBus[0] + synthBus[1] + synthBus[2] + drumBus +
                                 sampleBus;
            const int16_t dryPcm = toPcm(dryMix);
            const int32_t loopPcm = loopEngineProcessFrame(dryPcm);
            float mix = dryMix + static_cast<float>(loopPcm) / 12000.0f;

            // soft clip
            if (mix > 1.0f) mix = 1.0f; else if (mix < -1.0f) mix = -1.0f;
            buf[i] = toPcm(mix);
            s_stemBuf[i].master = buf[i];
            s_stemBuf[i].synth1 = toPcm(synthBus[0]);
            s_stemBuf[i].synth2 = toPcm(synthBus[1]);
            s_stemBuf[i].synth3 = toPcm(synthBus[2]);
            s_stemBuf[i].drums = toPcm(drumBus);

            if ((i & 7) == 0 && g_scopeIdx < SCREEN_W)
                g_scopeBuf[g_scopeIdx++] = mix;
        }

        streamingSamplerRecordPush(STREAM_SAMPLE_INPUT_BUS, buf, AUDIO_BUF_LEN);
        masterRecorderPush(buf, AUDIO_BUF_LEN);
        stemRecorderPush(s_stemBuf, AUDIO_BUF_LEN);

        while (!M5Cardputer.Speaker.playRaw(buf, AUDIO_BUF_LEN, SAMPLE_RATE, false, 1, 0))
            vTaskDelay(1);

        cur ^= 1;
    }
}

void audioEngineStart() {
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 1, &s_task, 0);
}
