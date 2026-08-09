#include "serial_control.h"

#include "control_protocol.h"
#include "master_recorder.h"
#include "sd_diagnostics.h"
#include "sequencer.h"
#include "midi_input.h"
#include "serial_line_buffer.h"
#include "stem_recorder.h"

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
