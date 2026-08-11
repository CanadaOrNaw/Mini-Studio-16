#include "serial_control.h"

#include "control_protocol.h"
#include "master_recorder.h"
#include "sd_diagnostics.h"
#include "sequencer.h"
#include "midi_input.h"
#include "serial_line_buffer.h"
#include "stem_recorder.h"
#include "loop_engine.h"
#include "streaming_sampler.h"
#include "event_looper.h"
#include "motion.h"
#include "ble_midi.h"
#include "usb_midi.h"
#include "mic_sampler.h"
#include "sd_io_arbiter.h"
#include "storage.h"
#include "audio_engine.h"
#include "synth_parameters.h"
#include "boot_selector.h"
#include "audio_cap.h"

#include <Arduino.h>
#include <M5Cardputer.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
SerialLineBuffer<CONTROL_LINE_MAX> s_line;

void replyError(const char* id, const char* error) {
    Serial.printf(CONTROL_PROTOCOL_PREFIX " %s ERR %s\n",
                  (id && *id) ? id : "-", error);
}

void dispatch(const ControlRequest& request) {
    switch (request.command) {
        case CONTROL_PING:
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK pong=1 firmware=v3-alpha\n", request.id);
            break;

        case CONTROL_STATUS: {
            const MasterRecorderSnapshot recorder = masterRecorderSnapshot();
            const StemRecorderSnapshot stems = stemRecorderSnapshot();
            const SdIoSnapshot sdIo = sdIoSnapshot();
            Serial.printf(
                CONTROL_PROTOCOL_PREFIX " %s OK playing=%u bpm=%u pattern=%u step=%u "
                "song=%u master=%s frames=%lu dropped=%lu path=%s midiDropped=%lu "
                "recoveredFrames=%lu recoveredPath=%s stems=%s stemFrames=%lu "
                "stemDropped=%lu stemPath=%s sdWaitMax=%lu sdHoldMax=%lu "
                "sdCalls=%lu sdContention=%lu heapFree=%lu heapLargest=%lu "
                "battery=%u uptimeMs=%lu project=%u\n",
                request.id, g_playing ? 1u : 0u, static_cast<unsigned>(g_bpm),
                static_cast<unsigned>(g_playPattern + 1),
                static_cast<unsigned>(g_playStep + 1), g_songMode ? 1u : 0u,
                masterRecorderStateName(recorder.state),
                static_cast<unsigned long>(recorder.framesWritten),
                static_cast<unsigned long>(recorder.droppedFrames),
                recorder.path[0] ? recorder.path : "-",
                static_cast<unsigned long>(midiInputDroppedEvents()),
                static_cast<unsigned long>(recorder.recoveredFrames),
                recorder.recoveredPath[0] ? recorder.recoveredPath : "-",
                stemRecorderStateName(stems.state),
                static_cast<unsigned long>(stems.framesWritten),
                static_cast<unsigned long>(stems.droppedFrames),
                stems.path[0] ? stems.path : "-",
                static_cast<unsigned long>(sdIo.maxWaitUs),
                static_cast<unsigned long>(sdIo.maxHoldUs),
                static_cast<unsigned long>(sdIo.acquisitions),
                static_cast<unsigned long>(sdIo.contentions),
                static_cast<unsigned long>(heap_caps_get_free_size(
                    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
                static_cast<unsigned long>(heap_caps_get_largest_free_block(
                    MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL)),
                static_cast<unsigned>(constrain(M5.Power.getBatteryLevel(), 0, 100)),
                static_cast<unsigned long>(millis()),
                static_cast<unsigned>(g_curProject + 1u));
            break;
        }

        case CONTROL_TRANSPORT_START:
            sequencerStart(true);
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK transport=started\n", request.id);
            break;

        case CONTROL_TRANSPORT_CONTINUE:
            sequencerStart(false);
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK transport=continued\n", request.id);
            break;

        case CONTROL_TRANSPORT_STOP:
            sequencerStop();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK transport=stopped\n", request.id);
            break;

        case CONTROL_TEMPO_SET:
            g_bpm = request.arg1;
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK bpm=%u\n", request.id,
                          static_cast<unsigned>(g_bpm));
            break;

        case CONTROL_NOTE_ON: {
            const uint8_t midi = static_cast<uint8_t>(request.arg2);
            const uint8_t note = static_cast<uint8_t>((midi % 12) + 1);
            const uint8_t octave = static_cast<uint8_t>((midi / 12) - 1);
            liveSynthNote(static_cast<uint8_t>(request.arg1 - 1), note, octave,
                          request.arg3 >= 100, false,
                          static_cast<uint8_t>(request.arg3));
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK note=%u track=%u\n", request.id,
                          static_cast<unsigned>(midi), static_cast<unsigned>(request.arg1));
            break;
        }

        case CONTROL_NOTE_OFF:
            liveSynthRelease(static_cast<uint8_t>(request.arg1 - 1),
                             static_cast<uint8_t>(request.arg2));
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK note_off=%u track=%u\n",
                          request.id, static_cast<unsigned>(request.arg2),
                          static_cast<unsigned>(request.arg1));
            break;

        case CONTROL_DRUM_HIT:
            liveDrumHit(static_cast<uint8_t>(request.arg1 - 1));
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK drum=%u\n", request.id,
                          static_cast<unsigned>(request.arg1));
            break;

        case CONTROL_SD_TEST:
            if (sdDiagnosticsStart())
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK sd_test=started\n", request.id);
            else
                replyError(request.id, "sd_test_busy_or_unavailable");
            break;

        case CONTROL_MASTER_START:
            if (masterRecorderStart()) {
                const MasterRecorderSnapshot recorder = masterRecorderSnapshot();
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK master=starting path=%s\n",
                              request.id, recorder.path);
            } else {
                replyError(request.id, "master_busy_or_unavailable");
            }
            break;

        case CONTROL_MASTER_STOP:
            if (masterRecorderStop())
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK master=stopping\n", request.id);
            else
                replyError(request.id, "master_not_recording");
            break;

        case CONTROL_STEM_START:
            if (stemRecorderStart()) {
                const StemRecorderSnapshot stems = stemRecorderSnapshot();
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK stems=starting path=%s\n",
                              request.id, stems.path);
            } else {
                replyError(request.id, "stems_busy_or_unavailable");
            }
            break;

        case CONTROL_STEM_STOP:
            if (stemRecorderStop())
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK stems=stopping\n", request.id);
            else
                replyError(request.id, "stems_not_recording");
            break;

        case CONTROL_LOOP_STATUS: {
            const LoopEngineSnapshot loops = loopEngineSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK available=%u timeline=%lu "
                          "absolute=%lu record=%u maxRead=%lu maxWrite=%lu errors=%lu",
                          request.id, loops.available ? 1u : 0u,
                          static_cast<unsigned long>(loops.timelineFrames),
                          static_cast<unsigned long>(loops.absoluteFrame),
                          loops.recordTrack == LOOP_NO_TRACK ? 0u : loops.recordTrack + 1u,
                          static_cast<unsigned long>(loops.maxReadUs),
                          static_cast<unsigned long>(loops.maxWriteUs),
                          static_cast<unsigned long>(loops.errors));
            for (uint8_t track = 0; track < LOOP_STREAM_TRACKS; ++track) {
                const LoopStreamTrackSnapshot& item = loops.tracks[track];
                Serial.printf(" t%u=%s,%lu,%lu,%lu,%lu,%lu,%u",
                              static_cast<unsigned>(track + 1),
                              loopEngineStateName(item.state),
                              static_cast<unsigned long>(item.lengthFrames),
                              static_cast<unsigned long>(item.capturedFrames),
                              static_cast<unsigned long>(item.droppedFrames),
                              static_cast<unsigned long>(item.underruns),
                              static_cast<unsigned long>(item.ringFrames),
                              static_cast<unsigned>(item.volumeQ15) * 100u / 32767u);
            }
            Serial.println("");
            break;
        }

        case CONTROL_LOOP_RECORD:
            if (loopEngineRequestRecord(static_cast<uint8_t>(request.arg1 - 1)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK loop=%u state=queued\n",
                              request.id, static_cast<unsigned>(request.arg1));
            else
                replyError(request.id, "loop_record_rejected");
            break;

        case CONTROL_LOOP_STOP:
            if (loopEngineStopRecording(static_cast<uint8_t>(request.arg1 - 1)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK loop=%u state=finalizing\n",
                              request.id, static_cast<unsigned>(request.arg1));
            else
                replyError(request.id, "loop_not_recording");
            break;

        case CONTROL_LOOP_MUTE:
        case CONTROL_LOOP_UNMUTE: {
            const bool muted = request.command == CONTROL_LOOP_MUTE;
            if (loopEngineSetMuted(static_cast<uint8_t>(request.arg1 - 1), muted))
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK loop=%u state=%s\n",
                              request.id, static_cast<unsigned>(request.arg1),
                              muted ? "muted" : "playing");
            else
                replyError(request.id, "loop_state_rejected");
            break;
        }

        case CONTROL_LOOP_CLEAR:
            if (loopEngineClear(static_cast<uint8_t>(request.arg1 - 1)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK loop=%u state=clearing\n",
                              request.id, static_cast<unsigned>(request.arg1));
            else
                replyError(request.id, "loop_clear_rejected");
            break;

        case CONTROL_LOOP_VOLUME:
            if (loopEngineSetVolume(static_cast<uint8_t>(request.arg1 - 1),
                                    static_cast<uint8_t>(request.arg2)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK loop=%u volume=%u\n", request.id,
                              static_cast<unsigned>(request.arg1),
                              static_cast<unsigned>(request.arg2));
            else
                replyError(request.id, "loop_volume_rejected");
            break;

        case CONTROL_SAMPLE_STATUS: {
            const StreamingSamplerSnapshot sampler = streamingSamplerSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK available=%u quota=%lu "
                          "remaining=%lu queued=%lu drops=%lu starts=%lu errors=%lu "
                          "maxRead=%lu record=%u,input=%u,slot=%u,frames=%lu,target=%lu,"
                          "dropped=%lu",
                          request.id, sampler.available ? 1u : 0u,
                          static_cast<unsigned long>(g_samplerSlotBank.quotaUsedFrames()),
                          static_cast<unsigned long>(g_samplerSlotBank.quotaRemainingFrames()),
                          static_cast<unsigned long>(sampler.queuedCommands),
                          static_cast<unsigned long>(sampler.commandDrops),
                          static_cast<unsigned long>(sampler.starts),
                          static_cast<unsigned long>(sampler.errors),
                          static_cast<unsigned long>(sampler.maxReadUs),
                          static_cast<unsigned>(sampler.recordState),
                          static_cast<unsigned>(sampler.recordInput),
                          static_cast<unsigned>(sampler.recordSlot + 1u),
                          static_cast<unsigned long>(sampler.recordFrames),
                          static_cast<unsigned long>(sampler.recordTargetFrames),
                          static_cast<unsigned long>(sampler.recordDroppedFrames));
            for (uint8_t voice = 0; voice < STREAMING_SAMPLE_VOICES; ++voice) {
                const SampleStreamVoiceSnapshot& item = sampler.voices[voice];
                Serial.printf(" v%u=%s,%u,%lu,%lu,%lu",
                              static_cast<unsigned>(voice + 1),
                              sampleStreamStateName(item.state),
                              static_cast<unsigned>(item.slot + 1),
                              static_cast<unsigned long>(item.consumedFrames),
                              static_cast<unsigned long>(item.bufferedFrames),
                              static_cast<unsigned long>(item.underruns));
            }
            Serial.println("");
            break;
        }

        case CONTROL_SAMPLE_ASSIGN:
            if (streamingSamplerAssign(static_cast<uint8_t>(request.arg1 - 1),
                                       request.text,
                                       static_cast<SamplerSlotMode>(request.arg2)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK sample=%u state=assigning file=%s\n",
                              request.id, static_cast<unsigned>(request.arg1), request.text);
            else
                replyError(request.id, "sample_assign_rejected");
            break;

        case CONTROL_SAMPLE_TRIGGER:
            if (streamingSamplerTrigger(static_cast<uint8_t>(request.arg1 - 1),
                                        static_cast<uint8_t>(request.arg2 - 1))) {
                eventLooperRecordSample(sequencerEventRecordStep(),
                                        static_cast<uint8_t>(request.arg1 - 1),
                                        static_cast<uint8_t>(request.arg2 - 1), 127);
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK sample=%u key=%u state=queued\n",
                              request.id, static_cast<unsigned>(request.arg1),
                              static_cast<unsigned>(request.arg2));
            } else
                replyError(request.id, "sample_trigger_rejected");
            break;

        case CONTROL_SAMPLE_CLEAR:
            if (streamingSamplerClear(static_cast<uint8_t>(request.arg1 - 1)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK sample=%u state=clearing\n", request.id,
                              static_cast<unsigned>(request.arg1));
            else
                replyError(request.id, "sample_clear_rejected");
            break;

        case CONTROL_SAMPLE_RECORD: {
            const uint8_t slot = static_cast<uint8_t>(request.arg1 - 1);
            const SamplerSlotMode mode = static_cast<SamplerSlotMode>(request.arg3);
            const bool started = request.arg2 == STREAM_SAMPLE_INPUT_MIC
                ? micStreamRecStart(slot, mode)
                : streamingSamplerBeginRecord(slot, mode, SAMPLE_RATE,
                                              STREAM_SAMPLE_INPUT_BUS);
            if (started)
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK sample=%u record=%s mode=%s\n", request.id,
                              static_cast<unsigned>(request.arg1),
                              request.arg2 == STREAM_SAMPLE_INPUT_MIC ? "mic" : "bus",
                              mode == SAMPLER_SLOT_SLICED ? "sliced" : "melodic");
            else replyError(request.id, "sample_record_rejected");
            break;
        }

        case CONTROL_SAMPLE_STOP: {
            const StreamingSamplerSnapshot sampler = streamingSamplerSnapshot();
            bool stopped = false;
            if (sampler.recordSlot == request.arg1 - 1u &&
                sampler.recordInput == STREAM_SAMPLE_INPUT_MIC && micRecActive()) {
                micRecStop();
                stopped = true;
            } else if (sampler.recordSlot == request.arg1 - 1u) {
                stopped = streamingSamplerStopRecord();
            }
            if (stopped)
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK sample=%u state=stopping\n", request.id,
                              static_cast<unsigned>(request.arg1));
            else replyError(request.id, "sample_not_recording");
            break;
        }

        case CONTROL_EVENT_STATUS:
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK position=%u count=%u capacity=%u",
                          request.id, static_cast<unsigned>(g_eventLoopPosition),
                          static_cast<unsigned>(g_eventLooper.count()),
                          static_cast<unsigned>(EVENT_LOOP_CAPACITY));
            for (uint8_t track = 0; track < EVENT_LOOP_TRACKS; ++track) {
                const EventLoopTrackState& state = g_eventLooper.track(track);
                Serial.printf(" t%u=%s,%u,%u,%u,%u",
                              static_cast<unsigned>(track + 1),
                              eventLooperRoleName(track),
                              static_cast<unsigned>(g_eventLooper.bars(track)),
                              state.armed ? 1u : 0u, state.muted ? 1u : 0u,
                              static_cast<unsigned>(g_eventLooper.count(track)));
            }
            Serial.println("");
            break;

        case CONTROL_EVENT_ARM:
        case CONTROL_EVENT_DISARM: {
            const bool armed = request.command == CONTROL_EVENT_ARM;
            if (g_eventLooper.setArmed(static_cast<uint8_t>(request.arg1 - 1), armed))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK event=%u armed=%u\n", request.id,
                              static_cast<unsigned>(request.arg1), armed ? 1u : 0u);
            else replyError(request.id, "event_arm_rejected");
            break;
        }

        case CONTROL_EVENT_MUTE:
        case CONTROL_EVENT_UNMUTE: {
            const bool muted = request.command == CONTROL_EVENT_MUTE;
            if (g_eventLooper.setMuted(static_cast<uint8_t>(request.arg1 - 1), muted))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK event=%u muted=%u\n", request.id,
                              static_cast<unsigned>(request.arg1), muted ? 1u : 0u);
            else replyError(request.id, "event_mute_rejected");
            break;
        }

        case CONTROL_EVENT_CLEAR:
            if (g_eventLooper.clearTrack(static_cast<uint8_t>(request.arg1 - 1)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK event=%u state=cleared\n", request.id,
                              static_cast<unsigned>(request.arg1));
            else replyError(request.id, "event_clear_rejected");
            break;

        case CONTROL_EVENT_BARS:
            if (g_eventLooper.setBars(static_cast<uint8_t>(request.arg1 - 1), request.arg2))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK event=%u bars=%u\n", request.id,
                              static_cast<unsigned>(request.arg1),
                              static_cast<unsigned>(request.arg2));
            else replyError(request.id, "event_bars_rejected");
            break;

        case CONTROL_MOTION_STATUS: {
            const MotionSnapshot motion = motionSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK available=%u samples=%lu gestures=%u "
                          "tilt_x=%u tilt_y=%u accel=%u gyro=%u shake=%u slap=%u",
                          request.id, motion.available ? 1u : 0u,
                          static_cast<unsigned long>(motion.samples),
                          static_cast<unsigned>(motion.gestures),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_TILT_X]),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_TILT_Y]),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_ACCEL]),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_GYRO]),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_SHAKE]),
                          static_cast<unsigned>(motion.values[MOTION_SOURCE_SLAP]));
            for (uint8_t mapping = 0; mapping < MOTION_MAPPING_COUNT; ++mapping)
                Serial.printf(" m%u=%s,%s", static_cast<unsigned>(mapping + 1),
                              motionSourceName(motion.mappings[mapping].source),
                              motionTargetName(motion.mappings[mapping].target));
            Serial.println("");
            break;
        }

        case CONTROL_MOTION_MAP:
            if (motionSetMapping(static_cast<uint8_t>(request.arg1 - 1),
                                 static_cast<MotionSource>(request.arg2),
                                 static_cast<MotionTarget>(request.arg3)))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK motion=%u source=%s target=%s\n", request.id,
                              static_cast<unsigned>(request.arg1),
                              motionSourceName(request.arg2),
                              motionTargetName(request.arg3));
            else replyError(request.id, "motion_map_rejected");
            break;

        case CONTROL_MOTION_CLEAR:
            motionClearMapping(static_cast<uint8_t>(request.arg1 - 1));
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK motion=%u state=cleared\n", request.id,
                          static_cast<unsigned>(request.arg1));
            break;

        case CONTROL_MIDI_STATUS: {
            const BleMidiSnapshot ble = bleMidiSnapshot();
            const UsbMidiSnapshot usb = usbMidiSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK queueDrops=%lu usbAvailable=%u usbMounted=%u usbHost=%u "
                          "usbRx=%lu usbTx=%lu usbErrors=%lu bleAvailable=%u "
                          "bleConnected=%u bleRx=%lu bleTx=%lu bleMalformed=%lu "
                          "bleDrops=%lu clockDrops=%lu\n",
                          request.id, static_cast<unsigned long>(midiInputDroppedEvents()),
                          usb.available ? 1u : 0u, usb.mounted ? 1u : 0u,
                          usb.hostMode ? 1u : 0u,
                          static_cast<unsigned long>(usb.bytesReceived),
                          static_cast<unsigned long>(usb.messagesSent),
                          static_cast<unsigned long>(usb.sendErrors),
                          ble.available ? 1u : 0u, ble.connected ? 1u : 0u,
                          static_cast<unsigned long>(ble.packetsReceived),
                          static_cast<unsigned long>(ble.packetsSent),
                          static_cast<unsigned long>(ble.malformedPackets),
                          static_cast<unsigned long>(ble.droppedPackets),
                          static_cast<unsigned long>(sequencerMidiClockDropped()));
            break;
        }

        case CONTROL_PROJECT_STATUS: {
            uint8_t occupied = 0;
            for (uint8_t slot = 0; slot < NUM_PROJECT_SLOTS; ++slot)
                if (storageProjectExists(slot)) occupied |= static_cast<uint8_t>(1u << slot);
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK project=%u occupied=0x%02X\n", request.id,
                          static_cast<unsigned>(g_curProject + 1u),
                          static_cast<unsigned>(occupied));
            break;
        }

        case CONTROL_PROJECT_SAVE: {
            const uint8_t slot = static_cast<uint8_t>(request.arg1 - 1u);
            if (storageSaveProject(slot)) {
                g_curProject = slot;
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK project=%u state=saved\n", request.id,
                              static_cast<unsigned>(request.arg1));
            } else replyError(request.id, "project_save_rejected");
            break;
        }

        case CONTROL_PROJECT_LOAD: {
            const uint8_t slot = static_cast<uint8_t>(request.arg1 - 1u);
            if (storageLoadProject(slot)) {
                g_curProject = slot;
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK project=%u state=loaded\n", request.id,
                              static_cast<unsigned>(request.arg1));
            } else replyError(request.id, "project_load_rejected");
            break;
        }

        case CONTROL_BOOT_STATUS: {
            const BootSelectorSnapshot boot = bootSelectorSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK compiled=%s running=%s configured=%s "
                          "normal=%u host=%u layout=%u pending=%u platformError=%ld\n",
                          request.id, bootRoleName(boot.layout.compiledRole),
                          bootRoleName(boot.layout.runningSlotRole),
                          bootRoleName(boot.configuredBootRole),
                          boot.layout.normalValid ? 1u : 0u,
                          boot.layout.usbHostValid ? 1u : 0u,
                          boot.layoutMatchesBuild ? 1u : 0u,
                          boot.switchPending ? 1u : 0u,
                          static_cast<long>(boot.lastPlatformError));
            break;
        }

        case CONTROL_BOOT_NORMAL:
        case CONTROL_BOOT_USB_HOST: {
            const BootRuntimeActivity activity = {
                masterRecorderIsBusy(), stemRecorderIsBusy(), micRecActive(),
                loopEngineIsRecording(), streamingSamplerIsRecording(),
                micSamplerHasPendingCommit(), loopEngineHasPendingClear(),
                streamingSamplerHasPendingMutation(), sdDiagnosticsIsRunning(),
            };
            const BootRuntimeBlocker blocker = bootEvaluateRuntime(activity);
            if (blocker != BOOT_RUNTIME_READY) {
                replyError(request.id, bootRuntimeBlockerName(blocker));
                break;
            }
            const BootRole target = request.command == CONTROL_BOOT_USB_HOST
                ? BOOT_ROLE_USB_HOST : BOOT_ROLE_NORMAL;
            const BootSwitchDecision decision = bootSelectorPrepare(target);
            if (decision == BOOT_SWITCH_ALREADY_ACTIVE) {
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK boot=%s state=active\n", request.id,
                              bootRoleName(target));
                break;
            }
            if (decision != BOOT_SWITCH_READY) {
                replyError(request.id, bootSwitchDecisionName(decision));
                break;
            }
            sequencerStop();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK boot=%s state=restarting\n", request.id,
                          bootRoleName(target));
            Serial.flush();
            delay(100);
            bootSelectorRestart();
            break;
        }

        case CONTROL_SYNTH_STATUS: {
            const AudioDspSnapshot dsp = audioEngineDspSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK dspBlocks=%lu dspLastUs=%lu dspMaxUs=%lu "
                          "dspMisses=%lu dspDeadlineUs=%lu",
                          request.id, static_cast<unsigned long>(dsp.blocks),
                          static_cast<unsigned long>(dsp.lastRenderUs),
                          static_cast<unsigned long>(dsp.maxRenderUs),
                          static_cast<unsigned long>(dsp.deadlineMisses),
                          static_cast<unsigned long>(dsp.deadlineUs));
            for (uint8_t track = 0; track < NUM_SYNTHS; ++track) {
                const SynthTrack& synth = g_synths[track];
                Serial.printf(" t%u=%s,%u,%u,%u,%u",
                              static_cast<unsigned>(track + 1),
                              synthEngineName(synth.displayEngine()),
                              static_cast<unsigned>(synth.voices),
                              static_cast<unsigned>(synth.volume() * 100.0f + 0.5f),
                              static_cast<unsigned>(synth.cutoff() * 100.0f + 0.5f),
                              static_cast<unsigned>(synth.resonance() * 100.0f + 0.5f));
            }
            Serial.println("");
            break;
        }

        case CONTROL_SYNTH_ENGINE: {
            SynthTrack& synth = g_synths[request.arg1 - 1u];
            // P2-8: this handler runs on the serial task while core 0 renders;
            // the switch is applied at the audio task's next block boundary.
            synth.requestEngine(static_cast<SynthEngine>(request.arg2));
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK synth=%u engine=%s\n", request.id,
                          static_cast<unsigned>(request.arg1),
                          synthEngineName(synth.displayEngine()));
            break;
        }

        case CONTROL_SYNTH_SET: {
            SynthTrack& synth = g_synths[request.arg1 - 1u];
            const SynthParameter parameter = static_cast<SynthParameter>(request.arg2);
            if (synthSetParameter(synth, parameter, request.arg3))
                Serial.printf(CONTROL_PROTOCOL_PREFIX
                              " %s OK synth=%u parameter=%s value=%u\n", request.id,
                              static_cast<unsigned>(request.arg1),
                              synthParameterName(parameter),
                              static_cast<unsigned>(request.arg3));
            else replyError(request.id, "synth_parameter_rejected");
            break;
        }

        case CONTROL_SYNTH_DSP_RESET:
            audioEngineResetDspStats();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK dsp=reset\n", request.id);
            break;

        case CONTROL_CAP_STATUS: {
            const AudioCapSnapshot cap = audioCapSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX
                          " %s OK present=%u connected=%u discovering=%u adc=%u "
                          "fault=%u monitor=%u transfers=%lu errors=%lu "
                          "playDrops=%lu captureDrops=%lu playUnderruns=%lu "
                          "captureUnderruns=%lu sequenceGaps=%lu\n",
                          request.id, cap.present ? 1u : 0u,
                          cap.a2dpConnected ? 1u : 0u,
                          cap.discovering ? 1u : 0u, cap.adcLocked ? 1u : 0u,
                          cap.fault ? 1u : 0u, static_cast<unsigned>(cap.monitorPercent),
                          static_cast<unsigned long>(cap.transfers),
                          static_cast<unsigned long>(cap.transferErrors),
                          static_cast<unsigned long>(cap.playbackDrops),
                          static_cast<unsigned long>(cap.captureDrops),
                          static_cast<unsigned long>(cap.playbackUnderruns),
                          static_cast<unsigned long>(cap.captureUnderruns),
                          static_cast<unsigned long>(cap.sequenceGaps));
            break;
        }

        case CONTROL_CAP_PAIR:
            audioCapRequestPair();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK cap=pairing\n", request.id);
            break;

        case CONTROL_CAP_DISCONNECT:
            audioCapRequestDisconnect();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK cap=disconnecting\n", request.id);
            break;

        case CONTROL_CAP_MONITOR:
            audioCapSetMonitor(static_cast<uint8_t>(request.arg1));
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK monitor=%u\n", request.id,
                          static_cast<unsigned>(request.arg1));
            break;

        case CONTROL_CAP_CLEAR:
            audioCapClearStats();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK cap=cleared\n", request.id);
            break;

        default:
            replyError(request.id, "unsupported_command");
            break;
    }
}

void consumeLine(const char* line) {
    ControlRequest request = {};
    const ControlParseStatus status = controlParseLine(line, request);
    if (status == CONTROL_PARSE_OK) dispatch(request);
    else replyError(request.id, controlParseStatusName(status));
}
}  // namespace

void serialControlInit() {
    s_line.consume();
    Serial.println(CONTROL_PROTOCOL_PREFIX " READY firmware=v3-alpha");
}

void serialControlUpdate() {
    uint8_t budget = 64;
    while (budget-- && Serial.available() > 0) {
        const int value = Serial.read();
        if (value < 0) break;
        const SerialLineResult result = s_line.feed(static_cast<char>(value));
        if (result == SERIAL_LINE_READY) {
            consumeLine(s_line.line());
            s_line.consume();
        } else if (result == SERIAL_LINE_OVERFLOW) {
            replyError("-", "line_too_long");
        }
    }
}
