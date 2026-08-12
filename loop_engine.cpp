#include "loop_engine.h"

#include "config.h"
#include "master_recorder.h"
#include "mic_sampler.h"
#include "sd_diagnostics.h"
#include "sd_io_arbiter.h"
#include "streaming_sampler.h"
#include "stem_recorder.h"
#include "wav_file.h"

#include <Arduino.h>
#include <SD.h>
#include <string.h>

extern uint16_t g_bpm;

namespace {
constexpr uint32_t kPlaybackRingFrames = 1024;
constexpr uint32_t kRecordRingFrames = 4096;
constexpr size_t kReadFrames = 256;
constexpr size_t kWriteFrames = 1024;
constexpr uint32_t kPrimeFrames = 512;
constexpr uint32_t kMaximumLoopFrames = SAMPLE_RATE * 20UL;
// P3 (reconciliation report): a boundary can land arbitrarily close to
// "now"; if the storage task is preempted between reading the frame counter
// and arming, the start is missed and the track stays phase-shifted for
// good. Schedules landing closer than this margin (~2.9 ms) are pushed one
// full loop later — still boundary-aligned, never late.
constexpr uint32_t kScheduleMarginFrames = 64;

using FirmwareLoopCore = LoopStreamCore<kPlaybackRingFrames, kRecordRingFrames>;

FirmwareLoopCore s_core;
File s_playbackFiles[LOOP_STREAM_TRACKS];
// P3: playback wraps at the WAV data-chunk end, not the physical file end,
// so a hand-copied file with trailing bytes cannot leak them into audio or
// stretch the audible period past lengthFrames.
uint32_t s_playbackDataFrames[LOOP_STREAM_TRACKS] = {};
uint32_t s_playbackFramesLeft[LOOP_STREAM_TRACKS] = {};
File s_recordFile;
uint32_t s_recordFileFrames = 0;
alignas(4) uint32_t s_recordFileTrack = LOOP_NO_TRACK;
alignas(4) uint32_t s_recordRequests = 0;
// Only Track 1 can establish a fixed bar/count-in length. Tracks 2..6 always
// inherit its timeline, so per-track parameter arrays represented no valid
// operation and unnecessarily consumed deterministic SRAM.
alignas(4) uint32_t s_trackOneTargetFrames = 0;
alignas(4) uint32_t s_trackOneCountInFrames = 0;
alignas(4) uint32_t s_clearRequests = 0;
alignas(4) uint32_t s_clearOutstanding = 0;
portMUX_TYPE s_metricsMux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t s_task = nullptr;
bool s_sdMounted = false;
uint32_t s_maxReadUs = 0;
uint32_t s_maxWriteUs = 0;
uint32_t s_errors = 0;
alignas(4) uint32_t s_paused = 0;
alignas(4) uint32_t s_metronome = 0;
alignas(4) uint32_t s_soloTrack = LOOP_NO_TRACK;
uint8_t s_preSoloMutedMask = 0;

void trackPath(uint8_t track, char* output, size_t capacity) {
    snprintf(output, capacity, "%s/L%u.wav", DIR_LOOPS,
             static_cast<unsigned>(track + 1));
}

void tempPath(uint8_t track, char* output, size_t capacity) {
    snprintf(output, capacity, "%s/.L%u.tmp", DIR_LOOPS,
             static_cast<unsigned>(track + 1));
}

void noteLatency(bool write, uint32_t elapsedUs) {
    portENTER_CRITICAL(&s_metricsMux);
    uint32_t& maximum = write ? s_maxWriteUs : s_maxReadUs;
    if (elapsedUs > maximum) maximum = elapsedUs;
    portEXIT_CRITICAL(&s_metricsMux);
}

void noteError() {
    portENTER_CRITICAL(&s_metricsMux);
    ++s_errors;
    portEXIT_CRITICAL(&s_metricsMux);
}

bool ensureDirectories() {
    SdIoGuard guard;
    return (SD.exists(DIR_ROOT) || SD.mkdir(DIR_ROOT)) &&
           (SD.exists(DIR_LOOPS) || SD.mkdir(DIR_LOOPS));
}

bool readCanonicalHeader(File& file, WavMono16Info& info) {
    uint8_t header[WAV_PCM_HEADER_BYTES];
    const uint32_t started = micros();
    SdIoGuard guard;
    const bool ok = file.seek(0) && file.read(header, sizeof(header)) ==
                                      static_cast<int>(sizeof(header)) &&
                    wavParseCanonicalMono16Header(header, info) &&
                    info.sampleRate == SAMPLE_RATE && info.frames > 0 &&
                    file.size() >= WAV_PCM_HEADER_BYTES + info.frames * 2u;
    noteLatency(false, micros() - started);
    return ok;
}

void closePlayback(uint8_t track) {
    if (track < LOOP_STREAM_TRACKS && s_playbackFiles[track]) {
        SdIoGuard guard;
        s_playbackFiles[track].close();
    }
}

bool seekLoopDataStart(uint8_t track) {
    const uint32_t started = micros();
    bool ok = false;
    { SdIoGuard guard; ok = s_playbackFiles[track].seek(WAV_PCM_HEADER_BYTES); }
    noteLatency(false, micros() - started);
    if (ok) s_playbackFramesLeft[track] = s_playbackDataFrames[track];
    return ok;
}

bool openPlayback(uint8_t track, uint32_t requiredFrames) {
    char path[64];
    trackPath(track, path, sizeof(path));
    closePlayback(track);
    const uint32_t started = micros();
    { SdIoGuard guard; s_playbackFiles[track] = SD.open(path, FILE_READ); }
    noteLatency(false, micros() - started);
    if (!s_playbackFiles[track]) return false;
    WavMono16Info info = {};
    if (!readCanonicalHeader(s_playbackFiles[track], info) ||
        info.frames > kMaximumLoopFrames ||
        (requiredFrames && info.frames != requiredFrames)) {
        closePlayback(track);
        return false;
    }
    s_playbackDataFrames[track] = info.frames;
    s_playbackFramesLeft[track] = info.frames;
    return s_core.preparePlayback(track, info.frames);
}

bool refillTrack(uint8_t track) {
    if (!s_playbackFiles[track] || s_core.playbackFree(track) < kReadFrames)
        return false;
    int16_t frames[kReadFrames];
    size_t filled = 0;
    // P2-6 (reconciliation report): the EOF-wrap retry used to be unbounded;
    // a degraded card that keeps returning 0 while seeks succeed would spin
    // this loop forever and wedge every loop track's service. Two
    // consecutive wraps with no progress now fail the track instead.
    size_t filledAtLastWrap = SIZE_MAX;
    while (filled < kReadFrames) {
        if (s_playbackFramesLeft[track] == 0) {
            // Wrap at the data-chunk end (P3), never at the physical EOF.
            if (filled == filledAtLastWrap) return false;
            filledAtLastWrap = filled;
            if (!seekLoopDataStart(track)) return false;
            continue;
        }
        size_t wantedFrames = kReadFrames - filled;
        if (wantedFrames > s_playbackFramesLeft[track])
            wantedFrames = s_playbackFramesLeft[track];
        const size_t wantedBytes = wantedFrames * sizeof(int16_t);
        const uint32_t started = micros();
        int got = 0;
        { SdIoGuard guard; got = s_playbackFiles[track].read(
            reinterpret_cast<uint8_t*>(frames + filled), wantedBytes); }
        noteLatency(false, micros() - started);
        if (got < 0 || (got & 1) != 0) return false;
        if (got == 0) {
            // Physical EOF before the header-promised frame count: broken.
            return false;
        }
        const size_t gotFrames = static_cast<size_t>(got) / sizeof(int16_t);
        filled += gotFrames;
        s_playbackFramesLeft[track] -= static_cast<uint32_t>(gotFrames);
    }
    return s_core.pushPlayback(track, frames, filled) == filled;
}

uint32_t scheduleWithMargin(uint32_t now, uint32_t length) {
    uint32_t scheduled = FirmwareLoopCore::nextBoundary(now, length, true);
    if (scheduled - now < kScheduleMarginFrames) scheduled += length;
    return scheduled;
}

bool primeAndArm(uint8_t track) {
    while (s_core.snapshot(track).ringFrames < kPrimeFrames)
        if (!refillTrack(track)) return false;
    const uint32_t length = s_core.snapshot(track).lengthFrames;
    const uint32_t now = s_core.absoluteFrame();
    const uint32_t scheduled = now == 0 ? 0 : scheduleWithMargin(now, length);
    return s_core.armPlayback(track, scheduled);
}

void quarantineTemp(uint8_t track, const char* temp, bool recoverable,
                    uint32_t frames) {
    char candidate[64];
    for (uint16_t number = 1; number <= 999; ++number) {
        snprintf(candidate, sizeof(candidate), "%s/RECOVER_L%u_%03u.%s", DIR_LOOPS,
                 static_cast<unsigned>(track + 1), static_cast<unsigned>(number),
                 recoverable ? "wav" : "bad");
        SdIoGuard guard;
        if (SD.exists(candidate)) continue;
        if (!SD.rename(temp, candidate)) noteError();
        else Serial.printf("LOOP_RECOVERY track=%u path=%s frames=%lu state=%s\n",
                           static_cast<unsigned>(track + 1), candidate,
                           static_cast<unsigned long>(frames),
                           recoverable ? "RECOVERED" : "QUARANTINED");
        return;
    }
    noteError();
}

void recoverTemps() {
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        char temp[64];
        tempPath(track, temp, sizeof(temp));
        File file;
        { SdIoGuard guard; if (!SD.exists(temp)) continue; file = SD.open(temp, "r+"); }
        if (!file) { noteError(); continue; }
        const WavRecoveryPlan plan =
            wavPlanMono16Recovery([&file]() { SdIoGuard guard;
                return static_cast<uint32_t>(file.size()); }());
        bool recoverable = plan.recoverable && plan.frames <= kMaximumLoopFrames;
        if (recoverable) {
            uint8_t header[WAV_PCM_HEADER_BYTES];
            wavBuildMono16Header(header, SAMPLE_RATE, plan.frames);
            SdIoGuard guard;
            recoverable = file.seek(0) &&
                file.write(header, sizeof(header)) == sizeof(header);
            if (recoverable) file.flush();
        }
        { SdIoGuard guard; file.close(); }
        quarantineTemp(track, temp, recoverable, plan.frames);
    }
}

void loadExistingLoops() {
    char path[64];
    trackPath(0, path, sizeof(path));
    { SdIoGuard guard; if (!SD.exists(path)) return; }
    if (!openPlayback(0, 0)) return;
    const uint32_t timeline = s_core.snapshot(0).lengthFrames;
    if (!s_core.establishTimeline(timeline) || !primeAndArm(0)) {
        s_core.markError(0);
        noteError();
        return;
    }
    for (uint8_t track = 1; track < LOOP_STREAM_TRACKS; ++track) {
        trackPath(track, path, sizeof(path));
        { SdIoGuard guard; if (!SD.exists(path)) continue; }
        if (!openPlayback(track, timeline) || !primeAndArm(track)) {
            s_core.markError(track);
            noteError();
        }
    }
}

bool startRecording(uint8_t track) {
    if (__atomic_load_n(&s_recordFileTrack, __ATOMIC_ACQUIRE) != LOOP_NO_TRACK ||
        !ensureDirectories()) return false;
    char temp[64];
    tempPath(track, temp, sizeof(temp));
    { SdIoGuard guard; if (SD.exists(temp)) return false; }
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, SAMPLE_RATE, 0);
    const uint32_t started = micros();
    bool ok = false;
    {
        SdIoGuard guard;
        s_recordFile = SD.open(temp, FILE_WRITE);
        ok = s_recordFile &&
             s_recordFile.write(header, sizeof(header)) == sizeof(header);
    }
    noteLatency(true, micros() - started);
    if (!ok) {
        // P1-2: never leave the temp behind — its existence blocks every
        // future startRecording for this track until reboot.
        if (s_recordFile) { SdIoGuard guard; s_recordFile.close(); }
        { SdIoGuard guard; if (SD.exists(temp)) SD.remove(temp); }
        return false;
    }
    const uint32_t requestedTarget = track == 0 ? __atomic_exchange_n(
        &s_trackOneTargetFrames, 0u, __ATOMIC_ACQ_REL) : 0u;
    const uint32_t requestedCountIn = track == 0 ? __atomic_exchange_n(
        &s_trackOneCountInFrames, 0u, __ATOMIC_ACQ_REL) : 0u;
    const uint32_t timeline = s_core.timelineFrames();
    const uint32_t scheduled = track == 0
        ? s_core.absoluteFrame() + requestedCountIn :
        scheduleWithMargin(s_core.absoluteFrame(), timeline);
    const uint32_t target = track == 0
        ? (requestedTarget ? requestedTarget : kMaximumLoopFrames) : timeline;
    if (!s_core.beginRecording(track, scheduled, target)) {
        { SdIoGuard guard; s_recordFile.close(); }
        { SdIoGuard guard; if (SD.exists(temp)) SD.remove(temp); }  // P1-2
        return false;
    }
    __atomic_store_n(&s_recordFileTrack, track, __ATOMIC_RELEASE);
    s_recordFileFrames = 0;
    return true;
}

bool writeRecordedFrames() {
    int16_t frames[kWriteFrames];
    const size_t count = s_core.popRecorded(frames, kWriteFrames);
    if (!count) return true;
    const size_t bytes = count * sizeof(int16_t);
    const uint32_t started = micros();
    bool ok = false;
    { SdIoGuard guard; ok = s_recordFile.write(reinterpret_cast<uint8_t*>(frames), bytes) == bytes; }
    noteLatency(true, micros() - started);
    if (ok) s_recordFileFrames += static_cast<uint32_t>(count);
    return ok;
}

bool finalizeRecording(uint8_t track) {
    const LoopStreamTrackSnapshot take = s_core.snapshot(track);
    bool ok = take.droppedFrames == 0 && s_recordFileFrames == take.capturedFrames &&
              s_recordFileFrames > 0 && s_recordFileFrames <= kMaximumLoopFrames;
    if (track > 0) ok = ok && s_recordFileFrames == s_core.timelineFrames();
    uint8_t header[WAV_PCM_HEADER_BYTES];
    wavBuildMono16Header(header, SAMPLE_RATE, s_recordFileFrames);
    const uint32_t started = micros();
    {
        SdIoGuard guard;
        ok = ok && s_recordFile.seek(0) &&
             s_recordFile.write(header, sizeof(header)) == sizeof(header);
        if (ok) s_recordFile.flush();
        s_recordFile.close();
    }
    noteLatency(true, micros() - started);

    char temp[64];
    char finalPath[64];
    tempPath(track, temp, sizeof(temp));
    trackPath(track, finalPath, sizeof(finalPath));
    {
        SdIoGuard guard;
        if (ok && SD.exists(finalPath)) ok = false;  // never destroy an existing loop
        if (ok) ok = SD.rename(temp, finalPath);
    }
    if (ok && track == 0) ok = s_core.establishTimeline(s_recordFileFrames);
    if (ok) ok = s_core.completeRecording(track, s_recordFileFrames) &&
                 openPlayback(track, s_recordFileFrames) && primeAndArm(track);
    if (!ok) {
        s_core.markError(track);
        noteError();
        // P1-2 (reconciliation report): a failed finalize used to leave
        // .L{n}.tmp on the card; startRecording refuses while it exists and
        // recovery only runs at boot, so one ordinary early stop (overdub
        // shorter than the timeline, or a stop during RECORD_WAIT) locked
        // the track until reboot. Salvage a structurally valid take the
        // same way boot recovery does, otherwise delete the temp.
        bool tempPresent = false;
        { SdIoGuard guard; tempPresent = SD.exists(temp); }
        if (tempPresent) {
            bool salvage = s_recordFileFrames > 0 &&
                           s_recordFileFrames <= kMaximumLoopFrames;
            if (salvage) {
                SdIoGuard guard;
                File file = SD.open(temp, "r+");
                salvage = file && file.seek(0) &&
                          file.write(header, sizeof(header)) == sizeof(header);
                if (file) {
                    if (salvage) file.flush();
                    file.close();
                }
            }
            if (salvage) {
                quarantineTemp(track, temp, true, s_recordFileFrames);
            } else {
                SdIoGuard guard;
                if (!SD.remove(temp)) noteError();
            }
        }
    }
    __atomic_store_n(&s_recordFileTrack, LOOP_NO_TRACK, __ATOMIC_RELEASE);
    s_recordFileFrames = 0;
    return ok;
}

void processClearRequests() {
    const uint32_t rawRequests =
        __atomic_exchange_n(&s_clearRequests, 0u, __ATOMIC_ACQ_REL);
    if (!rawRequests) return;
    uint32_t requests = rawRequests;
    if (requests & 1u) requests = (1u << LOOP_STREAM_TRACKS) - 1u;
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
        if ((requests & (1u << track)) == 0) continue;
        closePlayback(track);
        if (!s_core.clearTrack(track)) { noteError(); continue; }
        char path[64];
        trackPath(track, path, sizeof(path));
        SdIoGuard guard;
        if (SD.exists(path) && !SD.remove(path)) noteError();
    }
    __atomic_sub_fetch(&s_clearOutstanding,
                       static_cast<uint32_t>(__builtin_popcount(rawRequests)),
                       __ATOMIC_ACQ_REL);
}

void storageTask(void*) {
    if (!ensureDirectories()) noteError();
    else {
        recoverTemps();
        loadExistingLoops();
    }

    while (true) {
        processClearRequests();
        const uint32_t recordRequests =
            __atomic_exchange_n(&s_recordRequests, 0u, __ATOMIC_ACQ_REL);
        for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
            if ((recordRequests & (1u << track)) && !startRecording(track)) {
                s_core.markError(track);
                noteError();
            }
        }

        const uint8_t activeRecordTrack = static_cast<uint8_t>(
            __atomic_load_n(&s_recordFileTrack, __ATOMIC_ACQUIRE));
        if (activeRecordTrack != LOOP_NO_TRACK) {
            const uint8_t track = activeRecordTrack;
            const LoopStreamTrackSnapshot take = s_core.snapshot(track);
            if (track == 0 && take.capturedFrames >= kMaximumLoopFrames &&
                take.state == LOOP_STREAM_RECORDING)
                s_core.requestStopRecording(track);
            if (!writeRecordedFrames()) {
                { SdIoGuard guard; s_recordFile.close(); }
                s_core.markError(track);
                __atomic_store_n(&s_recordFileTrack, LOOP_NO_TRACK, __ATOMIC_RELEASE);
                noteError();
            } else if (s_core.trackState(track) == LOOP_STREAM_FINALIZING &&
                       s_core.recordedAvailable() == 0) {
                finalizeRecording(track);
            }
        }

        for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
            LoopStreamState state = s_core.trackState(track);
            if (state == LOOP_STREAM_UNDERRUN) {
                const bool seekOk = seekLoopDataStart(track);
                if (!s_core.prepareResync(track) || !seekOk) {
                    s_core.markError(track);
                    noteError();
                    continue;
                }
                if (!primeAndArm(track)) { s_core.markError(track); noteError(); }
                continue;
            }
            if (state == LOOP_STREAM_PREPARING || state == LOOP_STREAM_PLAY_WAIT ||
                state == LOOP_STREAM_PLAYING || state == LOOP_STREAM_MUTED) {
                if (s_core.playbackFree(track) >= kReadFrames && !refillTrack(track)) {
                    s_core.markError(track);
                    noteError();
                }
            }
        }
        vTaskDelay(1);
    }
}
}  // namespace

void loopEngineInit(bool sdMounted) {
    s_sdMounted = sdMounted;
    s_core.reset();
    __atomic_store_n(&s_recordFileTrack, LOOP_NO_TRACK, __ATOMIC_RELEASE);
    __atomic_store_n(&s_recordRequests, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_trackOneTargetFrames, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_trackOneCountInFrames, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_clearRequests, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_clearOutstanding, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_paused, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_metronome, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_soloTrack, LOOP_NO_TRACK, __ATOMIC_RELEASE);
    s_preSoloMutedMask = 0;
    portENTER_CRITICAL(&s_metricsMux);
    s_maxReadUs = s_maxWriteUs = s_errors = 0;
    portEXIT_CRITICAL(&s_metricsMux);
    if (!sdMounted) return;
    if (xTaskCreatePinnedToCore(storageTask, "loop_sd", 8192, nullptr, 1, &s_task, 1) != pdPASS) {
        s_task = nullptr;
        noteError();
    }
}

int32_t loopEngineProcessFrame(int16_t dryInput) {
    if (!s_sdMounted || __atomic_load_n(&s_paused, __ATOMIC_ACQUIRE)) return 0;
    int32_t output = s_core.processFrame(dryInput);
    if (__atomic_load_n(&s_metronome, __ATOMIC_ACQUIRE)) {
        const uint32_t beatFrames = static_cast<uint32_t>(SAMPLE_RATE) * 60u /
                                    (g_bpm ? g_bpm : 120u);
        const uint32_t phase = beatFrames ? s_core.absoluteFrame() % beatFrames : 0;
        if (phase < 96) output += static_cast<int32_t>(5000 * (96 - phase) / 96);
    }
    return output;
}
int32_t loopEngineLastTrackPcm(uint8_t track) { return s_core.lastTrackOutput(track); }

bool loopEngineRequestRecord(uint8_t track, uint32_t targetFrames,
                             uint32_t countInFrames) {
    if (!s_sdMounted || track >= LOOP_STREAM_TRACKS || loopEngineIsRecording() ||
        masterRecorderIsBusy() || stemRecorderIsBusy() || sdDiagnosticsIsRunning() ||
        micRecActive() || streamingSamplerIsRecording() ||
        __atomic_load_n(&s_recordRequests, __ATOMIC_ACQUIRE) != 0)
        return false;
    const LoopStreamState state = s_core.trackState(track);
    if (state != LOOP_STREAM_EMPTY && state != LOOP_STREAM_ERROR) return false;
    if (track > 0 && s_core.timelineFrames() == 0) return false;
    if (track > 0 && (targetFrames != 0 || countInFrames != 0)) return false;
    if (track == 0 && targetFrames > kMaximumLoopFrames) return false;
    if (track == 0) {
        __atomic_store_n(&s_trackOneTargetFrames, targetFrames, __ATOMIC_RELEASE);
        __atomic_store_n(&s_trackOneCountInFrames, countInFrames, __ATOMIC_RELEASE);
    }
    __atomic_fetch_or(&s_recordRequests, 1u << track, __ATOMIC_ACQ_REL);
    return true;
}

bool loopEngineStopRecording(uint8_t track) {
    if (track > 0 && track < LOOP_STREAM_TRACKS) {
        const LoopStreamState state = s_core.trackState(track);
        // Layers 2..6 are exact Track-1-length takes. An early button press
        // means "finish this layer" at the already scheduled boundary; an
        // immediate finalize would create a short file and reject it.
        if (state == LOOP_STREAM_RECORD_WAIT || state == LOOP_STREAM_RECORDING)
            return true;
    }
    return s_core.requestStopRecording(track);
}

bool loopEngineSetMuted(uint8_t track, bool muted) {
    return s_core.setMuted(track, muted);
}

bool loopEngineSetVolume(uint8_t track, uint8_t percent) {
    if (percent > 100) return false;
    return s_core.setVolumeQ15(track, static_cast<int16_t>(percent * 32767u / 100u));
}

bool loopEngineSetSolo(uint8_t track, bool solo) {
    if (track >= LOOP_STREAM_TRACKS) return false;
    const uint8_t current = static_cast<uint8_t>(
        __atomic_load_n(&s_soloTrack, __ATOMIC_ACQUIRE));
    if (solo) {
        if (current != LOOP_NO_TRACK && current != track) loopEngineSetSolo(current, false);
        s_preSoloMutedMask = 0;
        for (uint8_t index = 0; index < LOOP_STREAM_TRACKS; ++index) {
            if (s_core.muted(index)) s_preSoloMutedMask |= static_cast<uint8_t>(1u << index);
            s_core.setMuted(index, index != track);
        }
        __atomic_store_n(&s_soloTrack, track, __ATOMIC_RELEASE);
    } else {
        if (current != track) return false;
        for (uint8_t index = 0; index < LOOP_STREAM_TRACKS; ++index)
            s_core.setMuted(index, (s_preSoloMutedMask & (1u << index)) != 0);
        __atomic_store_n(&s_soloTrack, LOOP_NO_TRACK, __ATOMIC_RELEASE);
    }
    return true;
}

bool loopEngineSetPaused(bool paused) {
    if (loopEngineIsRecording()) return false;
    __atomic_store_n(&s_paused, paused ? 1u : 0u, __ATOMIC_RELEASE);
    return true;
}
void loopEngineSetMetronome(bool enabled) {
    __atomic_store_n(&s_metronome, enabled ? 1u : 0u, __ATOMIC_RELEASE);
}

bool loopEngineClear(uint8_t track) {
    if (track >= LOOP_STREAM_TRACKS || loopEngineIsRecording()) return false;
    const uint32_t request = 1u << track;
    // Count before publishing the request so the storage task cannot finish
    // and decrement an operation that the producer has not counted yet.
    __atomic_add_fetch(&s_clearOutstanding, 1u, __ATOMIC_ACQ_REL);
    const uint32_t previous =
        __atomic_fetch_or(&s_clearRequests, request, __ATOMIC_ACQ_REL);
    if ((previous & request) != 0)
        __atomic_sub_fetch(&s_clearOutstanding, 1u, __ATOMIC_ACQ_REL);
    return true;
}

bool loopEngineIsRecording() {
    return __atomic_load_n(&s_recordFileTrack, __ATOMIC_ACQUIRE) != LOOP_NO_TRACK ||
           s_core.recordTrack() != LOOP_NO_TRACK ||
           __atomic_load_n(&s_recordRequests, __ATOMIC_ACQUIRE) != 0;
}

bool loopEngineHasPendingClear() {
    return __atomic_load_n(&s_clearOutstanding, __ATOMIC_ACQUIRE) != 0;
}

bool loopEngineHasActiveIo() {
    if (loopEngineIsRecording()) return true;
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track)
        if (s_core.trackState(track) != LOOP_STREAM_EMPTY) return true;
    return false;
}

LoopEngineSnapshot loopEngineSnapshot() {
    LoopEngineSnapshot snapshot = {};
    snapshot.available = s_sdMounted && s_task != nullptr;
    snapshot.paused = __atomic_load_n(&s_paused, __ATOMIC_ACQUIRE) != 0;
    snapshot.metronome = __atomic_load_n(&s_metronome, __ATOMIC_ACQUIRE) != 0;
    snapshot.soloTrack = static_cast<uint8_t>(
        __atomic_load_n(&s_soloTrack, __ATOMIC_ACQUIRE));
    snapshot.recordTrack = s_core.recordTrack();
    if (snapshot.recordTrack == LOOP_NO_TRACK)
        snapshot.recordTrack = static_cast<uint8_t>(
            __atomic_load_n(&s_recordFileTrack, __ATOMIC_ACQUIRE));
    snapshot.timelineFrames = s_core.timelineFrames();
    snapshot.absoluteFrame = s_core.absoluteFrame();
    portENTER_CRITICAL(&s_metricsMux);
    snapshot.maxReadUs = s_maxReadUs;
    snapshot.maxWriteUs = s_maxWriteUs;
    snapshot.errors = s_errors;
    portEXIT_CRITICAL(&s_metricsMux);
    for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track)
        snapshot.tracks[track] = s_core.snapshot(track);
    return snapshot;
}

const char* loopEngineStateName(LoopStreamState state) {
    switch (state) {
        case LOOP_STREAM_EMPTY: return "empty";
        case LOOP_STREAM_PREPARING: return "preparing";
        case LOOP_STREAM_RECORD_WAIT: return "waiting";
        case LOOP_STREAM_RECORDING: return "recording";
        case LOOP_STREAM_FINALIZING: return "finalizing";
        case LOOP_STREAM_PLAY_WAIT: return "play_wait";
        case LOOP_STREAM_PLAYING: return "playing";
        case LOOP_STREAM_MUTED: return "muted";
        case LOOP_STREAM_UNDERRUN: return "underrun";
        case LOOP_STREAM_ERROR: return "error";
        default: return "unknown";
    }
}
