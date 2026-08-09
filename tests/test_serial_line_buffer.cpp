#include "../serial_line_buffer.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    SerialLineBuffer<16> buffer;
    const char* valid = "MS16/1 1 ping\n";
    SerialLineResult result = SERIAL_LINE_NONE;
    for (const char* cursor = valid; *cursor; ++cursor) result = buffer.feed(*cursor);
    assert(result == SERIAL_LINE_READY);
    assert(std::strcmp(buffer.line(), "MS16/1 1 ping") == 0);
    buffer.consume();

    for (int index = 0; index < 100; ++index) buffer.feed('x');
    assert(buffer.feed('\n') == SERIAL_LINE_OVERFLOW);

    for (int iteration = 0; iteration < 10000; ++iteration) {
        for (const char* cursor = valid; *cursor; ++cursor) result = buffer.feed(*cursor);
        assert(result == SERIAL_LINE_READY);
        buffer.consume();
    }
    std::cout << "serial_line_buffer: overflow recovery and 10000-line soak passed\n";
    return 0;
}
