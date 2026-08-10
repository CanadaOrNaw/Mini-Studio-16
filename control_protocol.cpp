#include "control_protocol.h"
#include "sampler_slots.h"
#include "event_looper_core.h"
#include "motion.h"
#include "streaming_sampler.h"
#include "storage.h"
#include "synth_parameters.h"

#include <stdlib.h>
#include <string.h>

namespace {
bool sameWord(const char* left, const char* right) {
    if (!left || !right) return false;
    while (*left && *right) {
        char a = *left++;
        char b = *right++;
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return *left == 0 && *right == 0;
}

char* nextToken(char*& cursor) {
    while (*cursor == ' ' || *cursor == '\t') ++cursor;
    if (*cursor == 0) return nullptr;
    char* token = cursor;
    while (*cursor && *cursor != ' ' && *cursor != '\t') ++cursor;
    if (*cursor) *cursor++ = 0;
    return token;
}

bool validId(const char* id) {
    if (!id || !*id || strlen(id) >= CONTROL_REQUEST_ID_LEN) return false;
    for (const char* p = id; *p; ++p) {
        const bool valid = (*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') ||
                           (*p >= '0' && *p <= '9') || *p == '-' || *p == '_';
        if (!valid) return false;
    }
    return true;
}

bool parseNumber(const char* token, uint16_t minimum, uint16_t maximum, uint16_t& value) {
    if (!token || !*token) return false;
    char* end = nullptr;
    const unsigned long parsed = strtoul(token, &end, 10);
    if (!end || *end != 0 || parsed < minimum || parsed > maximum) return false;
    value = static_cast<uint16_t>(parsed);
    return true;
}

bool noMore(char*& cursor) { return nextToken(cursor) == nullptr; }

bool parseMotionSource(const char* token, uint16_t& value) {
    if (sameWord(token, "tilt_x")) value = MOTION_SOURCE_TILT_X;
    else if (sameWord(token, "tilt_y")) value = MOTION_SOURCE_TILT_Y;
    else if (sameWord(token, "accel")) value = MOTION_SOURCE_ACCEL;
    else if (sameWord(token, "gyro")) value = MOTION_SOURCE_GYRO;
    else if (sameWord(token, "shake")) value = MOTION_SOURCE_SHAKE;
    else if (sameWord(token, "slap")) value = MOTION_SOURCE_SLAP;
    else return false;
    return true;
}

bool parseMotionTarget(const char* token, uint16_t& value) {
    if (sameWord(token, "synth1_cutoff")) value = MOTION_TARGET_SYNTH1_CUTOFF;
    else if (sameWord(token, "synth2_cutoff")) value = MOTION_TARGET_SYNTH2_CUTOFF;
    else if (sameWord(token, "synth3_cutoff")) value = MOTION_TARGET_SYNTH3_CUTOFF;
    else if (sameWord(token, "synth1_resonance")) value = MOTION_TARGET_SYNTH1_RESONANCE;
    else if (sameWord(token, "synth2_resonance")) value = MOTION_TARGET_SYNTH2_RESONANCE;
    else if (sameWord(token, "synth3_resonance")) value = MOTION_TARGET_SYNTH3_RESONANCE;
    else return false;
    return true;
}

bool parseSynthEngine(const char* token, uint16_t& value) {
    if (sameWord(token, "mg") || sameWord(token, "mg303") ||
        sameWord(token, "303")) value = SYNTH_ENGINE_MG;
    else if (sameWord(token, "mgx")) value = SYNTH_ENGINE_MGX;
    else if (sameWord(token, "fm") || sameWord(token, "fm4"))
        value = SYNTH_ENGINE_FM4;
    else return false;
    return true;
}
}  // namespace

ControlParseStatus controlParseLine(const char* line, ControlRequest& request) {
    memset(&request, 0, sizeof(request));
    if (!line) return CONTROL_PARSE_EMPTY;

    char buffer[CONTROL_LINE_MAX];
    size_t length = strlen(line);
    if (length == 0) return CONTROL_PARSE_EMPTY;
    if (length >= sizeof(buffer)) return CONTROL_PARSE_BAD_ARGUMENTS;
    memcpy(buffer, line, length + 1);

    while (length > 0 && (buffer[length - 1] == '\r' || buffer[length - 1] == '\n'))
        buffer[--length] = 0;
    if (length == 0) return CONTROL_PARSE_EMPTY;

    char* cursor = buffer;
    char* prefix = nextToken(cursor);
    if (!prefix) return CONTROL_PARSE_EMPTY;
    if (strcmp(prefix, CONTROL_PROTOCOL_PREFIX) != 0) return CONTROL_PARSE_BAD_PREFIX;

    char* id = nextToken(cursor);
    if (!id) return CONTROL_PARSE_MISSING_ID;
    if (!validId(id)) return CONTROL_PARSE_BAD_ID;
    strncpy(request.id, id, sizeof(request.id) - 1);

    char* verb = nextToken(cursor);
    if (!verb) return CONTROL_PARSE_UNKNOWN_COMMAND;

    if (sameWord(verb, "ping")) {
        if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_PING;
    } else if (sameWord(verb, "status")) {
        if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_STATUS;
    } else if (sameWord(verb, "transport")) {
        char* action = nextToken(cursor);
        if (!action || !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(action, "start")) request.command = CONTROL_TRANSPORT_START;
        else if (sameWord(action, "continue")) request.command = CONTROL_TRANSPORT_CONTINUE;
        else if (sameWord(action, "stop")) request.command = CONTROL_TRANSPORT_STOP;
        else return CONTROL_PARSE_BAD_ARGUMENTS;
    } else if (sameWord(verb, "tempo")) {
        if (!parseNumber(nextToken(cursor), 40, 300, request.arg1) || !noMore(cursor))
            return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_TEMPO_SET;
    } else if (sameWord(verb, "note")) {
        if (!parseNumber(nextToken(cursor), 1, 3, request.arg1) ||
            !parseNumber(nextToken(cursor), 24, 107, request.arg2) ||
            !parseNumber(nextToken(cursor), 1, 127, request.arg3) || !noMore(cursor))
            return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_NOTE_ON;
    } else if (sameWord(verb, "note_off")) {
        if (!parseNumber(nextToken(cursor), 1, 3, request.arg1) ||
            !parseNumber(nextToken(cursor), 24, 107, request.arg2) || !noMore(cursor))
            return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_NOTE_OFF;
    } else if (sameWord(verb, "drum")) {
        if (!parseNumber(nextToken(cursor), 1, 8, request.arg1) || !noMore(cursor))
            return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_DRUM_HIT;
    } else if (sameWord(verb, "sd_test")) {
        if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_SD_TEST;
    } else if (sameWord(verb, "master")) {
        char* action = nextToken(cursor);
        if (!action || !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(action, "start")) request.command = CONTROL_MASTER_START;
        else if (sameWord(action, "stop")) request.command = CONTROL_MASTER_STOP;
        else return CONTROL_PARSE_BAD_ARGUMENTS;
    } else if (sameWord(verb, "stems")) {
        char* action = nextToken(cursor);
        if (!action || !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(action, "start")) request.command = CONTROL_STEM_START;
        else if (sameWord(action, "stop")) request.command = CONTROL_STEM_STOP;
        else return CONTROL_PARSE_BAD_ARGUMENTS;
    } else if (sameWord(verb, "loop")) {
        char* trackOrStatus = nextToken(cursor);
        if (!trackOrStatus) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(trackOrStatus, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_LOOP_STATUS;
        } else {
            if (!parseNumber(trackOrStatus, 1, 6, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "volume")) {
                if (!parseNumber(nextToken(cursor), 0, 100, request.arg2) || !noMore(cursor))
                    return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_LOOP_VOLUME;
            } else if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            else if (sameWord(action, "record")) request.command = CONTROL_LOOP_RECORD;
            else if (sameWord(action, "stop")) request.command = CONTROL_LOOP_STOP;
            else if (sameWord(action, "mute")) request.command = CONTROL_LOOP_MUTE;
            else if (sameWord(action, "unmute")) request.command = CONTROL_LOOP_UNMUTE;
            else if (sameWord(action, "clear")) request.command = CONTROL_LOOP_CLEAR;
            else return CONTROL_PARSE_BAD_ARGUMENTS;
        }
    } else if (sameWord(verb, "sample")) {
        char* slotOrStatus = nextToken(cursor);
        if (!slotOrStatus) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(slotOrStatus, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_SAMPLE_STATUS;
        } else {
            if (!parseNumber(slotOrStatus, 1, SAMPLER_SLOT_COUNT, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "assign")) {
                char* filename = nextToken(cursor);
                char* mode = nextToken(cursor);
                if (!filename || strlen(filename) >= sizeof(request.text) || !mode ||
                    !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                strcpy(request.text, filename);
                if (sameWord(mode, "melodic")) request.arg2 = SAMPLER_SLOT_MELODIC;
                else if (sameWord(mode, "sliced")) request.arg2 = SAMPLER_SLOT_SLICED;
                else return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SAMPLE_ASSIGN;
            } else if (sameWord(action, "trigger")) {
                if (!parseNumber(nextToken(cursor), 1, SAMPLER_SLICE_COUNT, request.arg2) ||
                    !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SAMPLE_TRIGGER;
            } else if (sameWord(action, "clear")) {
                if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SAMPLE_CLEAR;
            } else if (sameWord(action, "record")) {
                char* input = nextToken(cursor);
                char* mode = nextToken(cursor);
                if (!input || !mode || !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                if (sameWord(input, "bus")) request.arg2 = STREAM_SAMPLE_INPUT_BUS;
                else if (sameWord(input, "mic")) request.arg2 = STREAM_SAMPLE_INPUT_MIC;
                else return CONTROL_PARSE_BAD_ARGUMENTS;
                if (sameWord(mode, "melodic")) request.arg3 = SAMPLER_SLOT_MELODIC;
                else if (sameWord(mode, "sliced")) request.arg3 = SAMPLER_SLOT_SLICED;
                else return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SAMPLE_RECORD;
            } else if (sameWord(action, "stop")) {
                if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SAMPLE_STOP;
            } else {
                return CONTROL_PARSE_BAD_ARGUMENTS;
            }
        }
    } else if (sameWord(verb, "event")) {
        char* trackOrStatus = nextToken(cursor);
        if (!trackOrStatus) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(trackOrStatus, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_EVENT_STATUS;
        } else {
            if (!parseNumber(trackOrStatus, 1, EVENT_LOOP_TRACKS, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "bars")) {
                if (!parseNumber(nextToken(cursor), 1, EVENT_LOOP_MAX_BARS, request.arg2) ||
                    !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_EVENT_BARS;
            } else {
                if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                if (sameWord(action, "arm")) request.command = CONTROL_EVENT_ARM;
                else if (sameWord(action, "disarm")) request.command = CONTROL_EVENT_DISARM;
                else if (sameWord(action, "mute")) request.command = CONTROL_EVENT_MUTE;
                else if (sameWord(action, "unmute")) request.command = CONTROL_EVENT_UNMUTE;
                else if (sameWord(action, "clear")) request.command = CONTROL_EVENT_CLEAR;
                else return CONTROL_PARSE_BAD_ARGUMENTS;
            }
        }
    } else if (sameWord(verb, "motion")) {
        char* mappingOrStatus = nextToken(cursor);
        if (!mappingOrStatus) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(mappingOrStatus, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_MOTION_STATUS;
        } else {
            if (!parseNumber(mappingOrStatus, 1, MOTION_MAPPING_COUNT, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "clear")) {
                if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_MOTION_CLEAR;
            } else if (sameWord(action, "map")) {
                if (!parseMotionSource(nextToken(cursor), request.arg2) ||
                    !parseMotionTarget(nextToken(cursor), request.arg3) || !noMore(cursor))
                    return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_MOTION_MAP;
            } else return CONTROL_PARSE_BAD_ARGUMENTS;
        }
    } else if (sameWord(verb, "midi")) {
        char* action = nextToken(cursor);
        if (!action || !sameWord(action, "status") || !noMore(cursor))
            return CONTROL_PARSE_BAD_ARGUMENTS;
        request.command = CONTROL_MIDI_STATUS;
    } else if (sameWord(verb, "project")) {
        char* slotOrStatus = nextToken(cursor);
        if (!slotOrStatus) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(slotOrStatus, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_PROJECT_STATUS;
        } else {
            if (!parseNumber(slotOrStatus, 1, NUM_PROJECT_SLOTS, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action || !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "save")) request.command = CONTROL_PROJECT_SAVE;
            else if (sameWord(action, "load")) request.command = CONTROL_PROJECT_LOAD;
            else return CONTROL_PARSE_BAD_ARGUMENTS;
        }
    } else if (sameWord(verb, "synth")) {
        char* trackOrAction = nextToken(cursor);
        if (!trackOrAction) return CONTROL_PARSE_BAD_ARGUMENTS;
        if (sameWord(trackOrAction, "status")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_SYNTH_STATUS;
        } else if (sameWord(trackOrAction, "dsp_reset")) {
            if (!noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
            request.command = CONTROL_SYNTH_DSP_RESET;
        } else {
            if (!parseNumber(trackOrAction, 1, 3, request.arg1))
                return CONTROL_PARSE_BAD_ARGUMENTS;
            char* action = nextToken(cursor);
            if (!action) return CONTROL_PARSE_BAD_ARGUMENTS;
            if (sameWord(action, "engine")) {
                if (!parseSynthEngine(nextToken(cursor), request.arg2) || !noMore(cursor))
                    return CONTROL_PARSE_BAD_ARGUMENTS;
                request.command = CONTROL_SYNTH_ENGINE;
            } else if (sameWord(action, "set")) {
                SynthParameter parameter = SYNTH_PARAM_ENGINE;
                char* parameterName = nextToken(cursor);
                if (!synthParameterFromName(parameterName, parameter))
                    return CONTROL_PARSE_BAD_ARGUMENTS;
                int32_t minimum = 0;
                int32_t maximum = 0;
                if (!synthParameterRange(parameter, minimum, maximum) || minimum < 0 ||
                    maximum > UINT16_MAX ||
                    !parseNumber(nextToken(cursor), static_cast<uint16_t>(minimum),
                                 static_cast<uint16_t>(maximum), request.arg3) ||
                    !noMore(cursor)) return CONTROL_PARSE_BAD_ARGUMENTS;
                request.arg2 = static_cast<uint16_t>(parameter);
                request.command = CONTROL_SYNTH_SET;
            } else return CONTROL_PARSE_BAD_ARGUMENTS;
        }
    } else {
        return CONTROL_PARSE_UNKNOWN_COMMAND;
    }

    return CONTROL_PARSE_OK;
}

const char* controlParseStatusName(ControlParseStatus status) {
    switch (status) {
        case CONTROL_PARSE_OK: return "ok";
        case CONTROL_PARSE_EMPTY: return "empty";
        case CONTROL_PARSE_BAD_PREFIX: return "bad_prefix";
        case CONTROL_PARSE_MISSING_ID: return "missing_id";
        case CONTROL_PARSE_BAD_ID: return "bad_id";
        case CONTROL_PARSE_UNKNOWN_COMMAND: return "unknown_command";
        case CONTROL_PARSE_BAD_ARGUMENTS: return "bad_arguments";
        default: return "parse_error";
    }
}
