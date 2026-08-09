#include "control_protocol.h"

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
