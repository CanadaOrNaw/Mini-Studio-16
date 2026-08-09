#include "../control_protocol.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

int main() {
    uint32_t random = 0x12345678u;
    char line[256];
    struct GuardedRequest {
        uint32_t before;
        ControlRequest request;
        uint32_t after;
    } guarded;

    for (uint32_t iteration = 0; iteration < 100000; ++iteration) {
        random ^= random << 13; random ^= random >> 17; random ^= random << 5;
        const size_t length = random % (sizeof(line) - 1);
        for (size_t index = 0; index < length; ++index) {
            random ^= random << 13; random ^= random >> 17; random ^= random << 5;
            line[index] = static_cast<char>(1 + (random % 126));
        }
        line[length] = 0;
        guarded.before = 0xA5A5A5A5u;
        guarded.after = 0x5A5A5A5Au;
        controlParseLine(line, guarded.request);
        assert(guarded.before == 0xA5A5A5A5u && guarded.after == 0x5A5A5A5Au);
    }

    assert(controlParseLine("MS16/1 final status", guarded.request) == CONTROL_PARSE_OK);
    assert(std::strcmp(guarded.request.id, "final") == 0);
    std::cout << "control_protocol: 100000 malformed-input fuzz cases passed\n";
    return 0;
}
