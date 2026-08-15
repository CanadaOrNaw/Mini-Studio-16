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
#include "audio_cap.h"
#include "performance_state.h"

float g_scopeBuf[SCREEN_W];
volatile int g_scopeIdx = 0;

static int16_t s_bufA[AUDIO_BUF_LEN];
static int16_t s_bufB[AUDIO_BUF_LEN];
static StemPcmFrame s_stemBuf[AUDIO_BUF_LEN];
static TaskHandle_t s_task = nullptr;
static portMUX_TYPE s_dspMux = portMUX_INITIALIZER_UNLOCKED;
static AudioDspSnapshot s_dsp = {
    0, 0, 0, 0,
    static_cast<uint32_t>(AUDIO_BUF_LEN) * 1000000u / SAMPLE_RATE,
};

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
        const uint32_t renderStartedUs = micros();

        // P2-8: engine switches requested by the UI/serial/storage tasks are
        // applied here, at the block boundary, so setEngine's voice re-init
        // can never interleave with the per-sample render() calls below.
        for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].applyPendingEngine();
        // A2-P1-5: voice hard-stops requested by the input task (HiChord
        // Lead/Drone chord changes) are applied here for the same reason.
        for (int s = 0; s < NUM_SYNTHS; s++) g_synths[s].applyPendingVoiceReset();
        // A2-P3: consume effect/vocoder settings once per block. The vocoder's
        // coefficient update runs 1 powf + 8 sinf and must never land inside
        // the per-sample loop below.
        g_masterEffects.syncSettings();
        g_vocoder.syncSettings();
        const VocoderSettings vocoder = g_vocoder.settings();

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
            // A2-P1-3 (alpha.2 reconciliation): the modulator used to be
            // hardcoded to 0 for anything but Loop 1, and the vocoder output
            // then REPLACED the dry bus — so selecting the mic or line source
            // silenced all three synths, eight drum lanes and the sampler.
            // A source that cannot currently supply audio now reports itself
            // unavailable and the dry bus passes through untouched.
            int16_t modulator = 0;
            bool haveModulator = false;
            if (vocoder.source == VOCODER_LOOP1) {
                modulator = static_cast<int16_t>(loopEngineLastTrackPcm(0));
                haveModulator = true;
            } else if (vocoder.source == VOCODER_LINE) {
                haveModulator = audioCapLineModulator(static_cast<size_t>(i), modulator);
            }
            // VOCODER_MIC needs simultaneous mic capture while the speaker
            // renders. The inherited high-level path switches between mic and
            // speaker rather than running both, so live mic modulation stays a
            // hardware gate (docs/CARDPUTER_TESTING.md section 11); until then
            // it behaves as "no modulator" and never mutes the instrument.
            const int16_t carrier = g_vocoder.process(toPcm(dryMix), modulator);
            const float carrierMix = (vocoder.enabled && haveModulator)
                ? static_cast<float>(carrier) / 32768.0f : dryMix;
            float mix = carrierMix + static_cast<float>(loopPcm) / 12000.0f;

            // soft clip
            if (mix > 1.0f) mix = 1.0f; else if (mix < -1.0f) mix = -1.0f;
            buf[i] = g_poEffectProcessor.process(g_masterEffects.process(toPcm(mix)));
            s_stemBuf[i].master = buf[i];
            s_stemBuf[i].synth1 = toPcm(synthBus[0]);
            s_stemBuf[i].synth2 = toPcm(synthBus[1]);
            s_stemBuf[i].synth3 = toPcm(synthBus[2]);
            s_stemBuf[i].drums = toPcm(drumBus);

            if ((i & 7) == 0 && g_scopeIdx < SCREEN_W)
                g_scopeBuf[g_scopeIdx++] = mix;
        }

        // The cap receives the dry master before its line input is mixed back,
        // preventing a Bluetooth feedback loop. Master/stem recording below
        // receives the monitored signal exactly as heard at the output.
        audioCapProcessAudioBlock(buf, AUDIO_BUF_LEN);
        streamingSamplerRecordPush(STREAM_SAMPLE_INPUT_BUS, buf, AUDIO_BUF_LEN);
        for (int i = 0; i < AUDIO_BUF_LEN; ++i) s_stemBuf[i].master = buf[i];
        masterRecorderPush(buf, AUDIO_BUF_LEN);
        stemRecorderPush(s_stemBuf, AUDIO_BUF_LEN);

        const uint32_t renderUs = micros() - renderStartedUs;
        portENTER_CRITICAL(&s_dspMux);
        ++s_dsp.blocks;
        s_dsp.lastRenderUs = renderUs;
        if (renderUs > s_dsp.maxRenderUs) s_dsp.maxRenderUs = renderUs;
        if (renderUs >= s_dsp.deadlineUs) ++s_dsp.deadlineMisses;
        portEXIT_CRITICAL(&s_dspMux);

        while (!M5Cardputer.Speaker.playRaw(buf, AUDIO_BUF_LEN, SAMPLE_RATE, false, 1, 0))
            vTaskDelay(1);

        cur ^= 1;
    }
}

void audioEngineStart() {
    xTaskCreatePinnedToCore(audioTask, "audio", 8192, nullptr, 1, &s_task, 0);
}

AudioDspSnapshot audioEngineDspSnapshot() {
    portENTER_CRITICAL(&s_dspMux);
    const AudioDspSnapshot result = s_dsp;
    portEXIT_CRITICAL(&s_dspMux);
    return result;
}

void audioEngineResetDspStats() {
    portENTER_CRITICAL(&s_dspMux);
    s_dsp.blocks = 0;
    s_dsp.lastRenderUs = 0;
    s_dsp.maxRenderUs = 0;
    s_dsp.deadlineMisses = 0;
    portEXIT_CRITICAL(&s_dspMux);
}
