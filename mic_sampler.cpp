// ============================================================
// Mini Studio 16 - microphone sampling and inherited short resampling
// ============================================================
#include "mic_sampler.h"

#include "loop_engine.h"
#include "master_recorder.h"
#include "sampler.h"
#include "sd_diagnostics.h"
#include "sequencer.h"
#include "stem_recorder.h"
#include "streaming_sampler.h"
#include "ui.h"
#include "performance_state.h"

#include <M5Cardputer.h>
#include <memory>
#include <new>
#include <string.h>

namespace {
constexpr uint8_t kNoLane = 0xFF;

bool s_recActive = false;
bool s_tunerActive = false;
std::unique_ptr<int16_t[]> s_tunerFrames;
uint16_t s_tunerCount = 0;
PitchEstimate s_tunerEstimate = {0.0f, 0.0f, -1, 0};
bool s_assignRecordedLane = false;
uint8_t s_recLane = kNoLane;
int8_t s_recReference = -1;
int16_t s_chunk[2][MIC_CAPTURE_CHUNK];
int s_curChunk = 0;
float s_level = 0.0f;

bool s_lanePublishPending = false;
uint8_t s_pendingLane = kNoLane;
int8_t s_pendingLaneReference = -1;

bool s_resampleActive = false;
bool s_resamplePending = false;
int8_t s_resampleReference = -1;
bool s_hiChordPublishPending = false;

bool recorderUnavailable() {
    return masterRecorderIsBusy() || stemRecorderIsBusy() ||
           sdDiagnosticsIsRunning() || loopEngineIsRecording();
}

void assignStreamToLane(uint8_t lane, int8_t reference) {
    if (lane >= NUM_DRUM_LANES || !samplerReferenceIsStreamed(reference)) return;
    DrumLane& drum = g_drumLanes[lane];
    drum.engine = ENG_SMPL;
    drum.sampleSlot = reference;
    drum.smp.init();
}

bool beginMic(uint8_t slot, SamplerSlotMode mode, uint32_t maximumFrames,
              bool assignLane, uint8_t lane, int8_t reference) {
    if (s_recActive || recorderUnavailable() ||
        !streamingSamplerBeginRecord(slot, mode, MIC_RATE,
                                     STREAM_SAMPLE_INPUT_MIC, maximumFrames,
                                     assignLane))
        return false;
    sequencerStop();
    M5Cardputer.Speaker.end();
    M5Cardputer.Mic.begin();
    s_recActive = true;
    s_assignRecordedLane = assignLane;
    s_recLane = lane;
    s_recReference = reference;
    s_curChunk = 0;
    s_level = 0.0f;
    M5Cardputer.Mic.record(s_chunk[0], MIC_CAPTURE_CHUNK, MIC_RATE);
    M5Cardputer.Mic.record(s_chunk[1], MIC_CAPTURE_CHUNK, MIC_RATE);
    uiStatus(assignLane ? "SAMPLING..." : "STREAM MIC...");
    return true;
}

void resolvePendingLane() {
    if (!s_lanePublishPending) return;
    const StreamingSamplerRecordState state = streamingSamplerSnapshot().recordState;
    if (state == STREAM_SAMPLE_REC_COMPLETE) {
        assignStreamToLane(s_pendingLane, s_pendingLaneReference);
        s_lanePublishPending = false;
        s_pendingLane = kNoLane;
        s_pendingLaneReference = -1;
        uiStatus("SAMPLED!");
        g_needRedraw = true;
    } else if (state == STREAM_SAMPLE_REC_ERROR) {
        samplerReleaseStreamReference(s_pendingLaneReference);
        s_lanePublishPending = false;
        s_pendingLane = kNoLane;
        s_pendingLaneReference = -1;
        uiStatus("SAMPLE FAILED");
        g_needRedraw = true;
    }
}

void resolveShortResample() {
    if (!s_resampleActive) return;
    const StreamingSamplerRecordState state = streamingSamplerSnapshot().recordState;
    if (state == STREAM_SAMPLE_REC_COMPLETE) {
        s_resampleActive = false;
        s_resamplePending = true;
        uiStatus("RSMP: TAP A PAD");
        g_needRedraw = true;
    } else if (state == STREAM_SAMPLE_REC_ERROR) {
        samplerReleaseStreamReference(s_resampleReference);
        s_resampleReference = -1;
        s_resampleActive = false;
        uiStatus("RESAMPLE FAILED");
        g_needRedraw = true;
    }
}

void resolveHiChordSample() {
    if (!s_hiChordPublishPending) return;
    const StreamingSamplerRecordState state = streamingSamplerSnapshot().recordState;
    if (state == STREAM_SAMPLE_REC_COMPLETE) {
        s_hiChordPublishPending = false;
        g_hiChordPerformance.setMode(HICHORD_LEAD);
        uiStatus("TUNED TO C - LEAD MODE");
        g_needRedraw = true;
    } else if (state == STREAM_SAMPLE_REC_ERROR) {
        s_hiChordPublishPending = false;
        uiStatus("MIC SAMPLE FAILED");
        g_needRedraw = true;
    }
}
}  // namespace

bool micSamplerInit() {
    // Legacy mic/pad sampling now uses the same bounded SD recorder as the
    // 16-slot sampler.  This preserves the original workflow without keeping
    // an 84 KiB whole-take scratch allocation alive on a no-PSRAM board.
    return true;
}

bool micRecStart(uint8_t lane) {
    if (lane >= NUM_DRUM_LANES || s_lanePublishPending) return false;
    const int8_t reference = samplerReserveStreamReference();
    uint8_t slot = 0;
    if (!samplerDecodeStreamReference(reference, slot)) return false;
    if (beginMic(slot, SAMPLER_SLOT_MELODIC, SCRATCH_FRAMES,
                 true, lane, reference))
        return true;
    samplerReleaseStreamReference(reference);
    return false;
}

bool micStreamRecStart(uint8_t slot, SamplerSlotMode mode) {
    if (slot >= SAMPLER_SLOT_COUNT || s_lanePublishPending ||
        s_resampleActive || s_resamplePending)
        return false;
    return beginMic(slot, mode, 0, false, kNoLane,
                    samplerMakeStreamReference(slot));
}

bool micHiChordRecStart(uint8_t slot) {
    if (slot >= SAMPLER_SLOT_COUNT || s_lanePublishPending ||
        s_resampleActive || s_resamplePending) return false;
    if (!beginMic(slot, SAMPLER_SLOT_MELODIC, MIC_RATE * 3u, false,
                  kNoLane, samplerMakeStreamReference(slot))) return false;
    s_hiChordPublishPending = true;
    return true;
}

bool micTunerStart() {
    if (s_recActive || recorderUnavailable()) return false;
    s_tunerFrames.reset(new (std::nothrow) int16_t[2048]);
    if (!s_tunerFrames) { uiStatus("TUNER: NO MEMORY"); return false; }
    sequencerStop(); M5Cardputer.Speaker.end(); M5Cardputer.Mic.begin();
    s_recActive = true; s_tunerActive = true; s_tunerCount = 0;
    s_tunerEstimate = {0.0f, 0.0f, -1, 0}; s_curChunk = 0;
    M5Cardputer.Mic.record(s_chunk[0], MIC_CAPTURE_CHUNK, MIC_RATE);
    M5Cardputer.Mic.record(s_chunk[1], MIC_CAPTURE_CHUNK, MIC_RATE);
    uiStatus("TUNER LISTENING"); return true;
}

PitchEstimate micTunerEstimate() { return s_tunerEstimate; }

void micSamplerUpdate() {
    if (s_recActive) {
        uint8_t stalledRequeues = 0;
        while (!M5Cardputer.Mic.isRecording() ||
               M5Cardputer.Mic.isRecording() == 1) {
            int16_t* done = s_chunk[s_curChunk];
            if (s_tunerActive) {
                const uint16_t available = static_cast<uint16_t>(2048 - s_tunerCount);
                const uint16_t copy = available < MIC_CAPTURE_CHUNK ? available : MIC_CAPTURE_CHUNK;
                memcpy(s_tunerFrames.get() + s_tunerCount, done, copy * sizeof(int16_t));
                s_tunerCount = static_cast<uint16_t>(s_tunerCount + copy);
                if (s_tunerCount == 2048) {
                    s_tunerEstimate = PitchDetector::detect(s_tunerFrames.get(), 2048, MIC_RATE);
                    memmove(s_tunerFrames.get(), s_tunerFrames.get() + 1024, 1024 * sizeof(int16_t));
                    s_tunerCount = 1024; g_needRedraw = true;
                }
                M5Cardputer.Mic.record(done, MIC_CAPTURE_CHUNK, MIC_RATE);
                s_curChunk ^= 1;
                if (M5Cardputer.Mic.isRecording() >= 2) break;
                if (++stalledRequeues >= 2) { micRecStop(); break; }
                continue;
            }
            const size_t pushed = streamingSamplerRecordPush(
                STREAM_SAMPLE_INPUT_MIC, done, MIC_CAPTURE_CHUNK);
            int32_t peak = 0;
            for (size_t index = 0; index < pushed; ++index) {
                const int32_t magnitude = done[index] < 0
                    ? -static_cast<int32_t>(done[index]) : done[index];
                if (magnitude > peak) peak = magnitude;
            }
            s_level = s_level * 0.6f + (peak / 32768.0f) * 0.4f;

            const StreamingSamplerRecordState state =
                streamingSamplerSnapshot().recordState;
            if (state == STREAM_SAMPLE_REC_STOPPING ||
                state == STREAM_SAMPLE_REC_ERROR) {
                micRecStop();
                break;
            }
            M5Cardputer.Mic.record(done, MIC_CAPTURE_CHUNK, MIC_RATE);
            s_curChunk ^= 1;
            if (M5Cardputer.Mic.isRecording() >= 2) break;
            // P3 (reconciliation report): if the driver refuses to arm the
            // next chunk, this loop would push the same buffer until the
            // frame target — a garbage take. Two consecutive failed
            // re-queues abort the capture instead.
            if (M5Cardputer.Mic.isRecording() == 0) {
                if (++stalledRequeues >= 2) { micRecStop(); break; }
            } else {
                stalledRequeues = 0;
            }
        }
        g_holdProg = s_level > 1.0f ? 1.0f : s_level;
        strncpy(g_holdLabel, "SAMPLING", sizeof(g_holdLabel));
        static uint32_t lastDraw = 0;
        if (millis() - lastDraw > 50) {
            lastDraw = millis();
            g_needRedraw = true;
        }
    }
    resolvePendingLane();
    resolveShortResample();
    resolveHiChordSample();
}

void micRecStop() {
    if (!s_recActive) return;
    s_recActive = false;
    M5Cardputer.Mic.end();
    M5Cardputer.Speaker.begin();
    g_holdProg = 0;
    g_holdLabel[0] = 0;

    if (s_tunerActive) {
        s_tunerActive = false; s_tunerFrames.reset();
        uiStatus("TUNER STOPPED"); g_needRedraw = true; return;
    }
    const StreamingSamplerRecordState state = streamingSamplerSnapshot().recordState;
    if (state == STREAM_SAMPLE_REC_STARTING || state == STREAM_SAMPLE_REC_RECORDING)
        streamingSamplerStopRecord();
    if (s_assignRecordedLane) {
        s_lanePublishPending = true;
        s_pendingLane = s_recLane;
        s_pendingLaneReference = s_recReference;
    }
    s_assignRecordedLane = false;
    s_recLane = kNoLane;
    s_recReference = -1;
    uiStatus("SAMPLE SAVING...");
    g_needRedraw = true;
}

bool micRecActive() { return s_recActive; }

bool micSamplerHasPendingCommit() {
    return s_lanePublishPending || s_resampleActive || s_resamplePending ||
           s_hiChordPublishPending;
}

void resampleArm() {
    if (s_recActive || s_resampleActive || s_resamplePending || recorderUnavailable() ||
        streamingSamplerIsRecording())
        return;
    const int8_t reference = samplerReserveStreamReference();
    uint8_t slot = 0;
    if (!samplerDecodeStreamReference(reference, slot)) return;
    if (!streamingSamplerBeginRecord(slot, SAMPLER_SLOT_MELODIC, SAMPLE_RATE,
                                     STREAM_SAMPLE_INPUT_BUS, SCRATCH_FRAMES, true)) {
        samplerReleaseStreamReference(reference);
        return;
    }
    s_resampleReference = reference;
    s_resampleActive = true;
    uiStatus("RESAMPLING...");
}

bool resamplePending() { return s_resamplePending; }

void resampleCommit(uint8_t lane) {
    if (!s_resamplePending || lane >= NUM_DRUM_LANES) return;
    assignStreamToLane(lane, s_resampleReference);
    s_resamplePending = false;
    s_resampleReference = -1;
    uiStatus("RESAMPLED!");
    g_needRedraw = true;
}
