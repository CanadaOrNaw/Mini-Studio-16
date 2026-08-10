#include "../control_protocol.h"

#include <cassert>
#include <cstring>
#include <iostream>

static ControlRequest parseOk(const char* line) {
    ControlRequest request = {};
    assert(controlParseLine(line, request) == CONTROL_PARSE_OK);
    return request;
}

int main() {
    ControlRequest request = parseOk("MS16/1 abc-12 ping\r\n");
    assert(std::strcmp(request.id, "abc-12") == 0);
    assert(request.command == CONTROL_PING);

    request = parseOk("MS16/1 2 TRANSPORT continue");
    assert(request.command == CONTROL_TRANSPORT_CONTINUE);

    request = parseOk("MS16/1 3 tempo 240");
    assert(request.command == CONTROL_TEMPO_SET && request.arg1 == 240);

    request = parseOk("MS16/1 note1 note 3 107 127");
    assert(request.command == CONTROL_NOTE_ON);
    assert(request.arg1 == 3 && request.arg2 == 107 && request.arg3 == 127);

    request = parseOk("MS16/1 5 drum 8");
    assert(request.command == CONTROL_DRUM_HIT && request.arg1 == 8);

    request = parseOk("MS16/1 6 master start");
    assert(request.command == CONTROL_MASTER_START);
    request = parseOk("MS16/1 7 stems stop");
    assert(request.command == CONTROL_STEM_STOP);
    request = parseOk("MS16/1 8 loop status");
    assert(request.command == CONTROL_LOOP_STATUS);
    request = parseOk("MS16/1 9 loop 6 record");
    assert(request.command == CONTROL_LOOP_RECORD && request.arg1 == 6);
    request = parseOk("MS16/1 10 loop 2 unmute");
    assert(request.command == CONTROL_LOOP_UNMUTE && request.arg1 == 2);

    ControlRequest invalid = {};
    assert(controlParseLine("", invalid) == CONTROL_PARSE_EMPTY);
    assert(controlParseLine("MS15/1 1 ping", invalid) == CONTROL_PARSE_BAD_PREFIX);
    assert(controlParseLine("MS16/1", invalid) == CONTROL_PARSE_MISSING_ID);
    assert(controlParseLine("MS16/1 bad/id ping", invalid) == CONTROL_PARSE_BAD_ID);
    assert(controlParseLine("MS16/1 1 tempo 39", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 note 1 23 100", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 drum 9", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 loop 0 record", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 loop 1 dance", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);
    assert(controlParseLine("MS16/1 1 wat", invalid) == CONTROL_PARSE_UNKNOWN_COMMAND);
    assert(controlParseLine("MS16/1 1 ping extra", invalid) == CONTROL_PARSE_BAD_ARGUMENTS);

    assert(std::strcmp(controlParseStatusName(CONTROL_PARSE_BAD_PREFIX), "bad_prefix") == 0);
    std::cout << "control_protocol: all tests passed\n";
    return 0;
}
