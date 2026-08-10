#pragma once

#include <stddef.h>
#include <stdint.h>
#include "config.h"

#define CONTROL_PROTOCOL_PREFIX "MS16/1"
#define CONTROL_REQUEST_ID_LEN 16
#define CONTROL_LINE_MAX 128

enum ControlCommand : uint8_t {
    CONTROL_NONE = 0,
    CONTROL_PING,
    CONTROL_STATUS,
    CONTROL_TRANSPORT_START,
    CONTROL_TRANSPORT_CONTINUE,
    CONTROL_TRANSPORT_STOP,
    CONTROL_TEMPO_SET,
    CONTROL_NOTE_ON,
    CONTROL_DRUM_HIT,
    CONTROL_SD_TEST,
    CONTROL_MASTER_START,
    CONTROL_MASTER_STOP,
    CONTROL_STEM_START,
    CONTROL_STEM_STOP,
    CONTROL_LOOP_STATUS,
    CONTROL_LOOP_RECORD,
    CONTROL_LOOP_STOP,
    CONTROL_LOOP_MUTE,
    CONTROL_LOOP_UNMUTE,
    CONTROL_LOOP_CLEAR,
    CONTROL_SAMPLE_STATUS,
    CONTROL_SAMPLE_ASSIGN,
    CONTROL_SAMPLE_TRIGGER,
    CONTROL_SAMPLE_CLEAR,
};

enum ControlParseStatus : uint8_t {
    CONTROL_PARSE_OK = 0,
    CONTROL_PARSE_EMPTY,
    CONTROL_PARSE_BAD_PREFIX,
    CONTROL_PARSE_MISSING_ID,
    CONTROL_PARSE_BAD_ID,
    CONTROL_PARSE_UNKNOWN_COMMAND,
    CONTROL_PARSE_BAD_ARGUMENTS,
};

struct ControlRequest {
    char id[CONTROL_REQUEST_ID_LEN];
    ControlCommand command;
    uint16_t arg1;
    uint16_t arg2;
    uint16_t arg3;
    char text[SAMPLE_NAME_LEN];
};

ControlParseStatus controlParseLine(const char* line, ControlRequest& request);
const char* controlParseStatusName(ControlParseStatus status);
