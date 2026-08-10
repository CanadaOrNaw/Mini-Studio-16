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

#include <Arduino.h>
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
            Serial.printf(
                CONTROL_PROTOCOL_PREFIX " %s OK playing=%u bpm=%u pattern=%u step=%u "
                "song=%u master=%s frames=%lu dropped=%lu path=%s midiDropped=%lu "
                "recoveredFrames=%lu recoveredPath=%s stems=%s stemFrames=%lu "
                "stemDropped=%lu stemPath=%s\n",
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
                stems.path[0] ? stems.path : "-");
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
                          request.arg3 >= 100, false);
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK note=%u track=%u\n", request.id,
                          static_cast<unsigned>(midi), static_cast<unsigned>(request.arg1));
            break;
        }

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
                Serial.printf(" t%u=%s,%lu,%lu,%lu,%lu,%lu",
                              static_cast<unsigned>(track + 1),
                              loopEngineStateName(item.state),
                              static_cast<unsigned long>(item.lengthFrames),
                              static_cast<unsigned long>(item.capturedFrames),
                              static_cast<unsigned long>(item.droppedFrames),
                              static_cast<unsigned long>(item.underruns),
                              static_cast<unsigned long>(item.ringFrames));
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

        case CONTROL_SAMPLE_STATUS: {
            const StreamingSamplerSnapshot sampler = streamingSamplerSnapshot();
            Serial.printf(CONTROL_PROTOCOL_PREFIX " %s OK available=%u quota=%lu "
                          "remaining=%lu queued=%lu drops=%lu starts=%lu errors=%lu "
                          "maxRead=%lu",
                          request.id, sampler.available ? 1u : 0u,
                          static_cast<unsigned long>(g_samplerSlotBank.quotaUsedFrames()),
                          static_cast<unsigned long>(g_samplerSlotBank.quotaRemainingFrames()),
                          static_cast<unsigned long>(sampler.queuedCommands),
                          static_cast<unsigned long>(sampler.commandDrops),
                          static_cast<unsigned long>(sampler.starts),
                          static_cast<unsigned long>(sampler.errors),
                          static_cast<unsigned long>(sampler.maxReadUs));
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
                          " %s OK queueDrops=%lu usbAvailable=%u usbMounted=%u "
                          "usbRx=%lu usbTx=%lu usbErrors=%lu bleAvailable=%u "
                          "bleConnected=%u bleRx=%lu bleTx=%lu bleMalformed=%lu "
                          "bleDrops=%lu\n",
                          request.id, static_cast<unsigned long>(midiInputDroppedEvents()),
                          usb.available ? 1u : 0u, usb.mounted ? 1u : 0u,
                          static_cast<unsigned long>(usb.bytesReceived),
                          static_cast<unsigned long>(usb.messagesSent),
                          static_cast<unsigned long>(usb.sendErrors),
                          ble.available ? 1u : 0u, ble.connected ? 1u : 0u,
                          static_cast<unsigned long>(ble.packetsReceived),
                          static_cast<unsigned long>(ble.packetsSent),
                          static_cast<unsigned long>(ble.malformedPackets),
                          static_cast<unsigned long>(ble.droppedPackets));
            break;
        }

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
