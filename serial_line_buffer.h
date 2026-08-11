#pragma once

#include <stddef.h>

enum SerialLineResult : unsigned char {
    SERIAL_LINE_NONE = 0,
    SERIAL_LINE_READY,
    SERIAL_LINE_OVERFLOW,
};

template <size_t Capacity>
class SerialLineBuffer {
    static_assert(Capacity >= 2, "serial line buffer is too small");
public:
    SerialLineBuffer() { reset(); }

    SerialLineResult feed(char character) {
        if (character == '\r') return SERIAL_LINE_NONE;
        if (character == '\n') {
            if (_overflow) { reset(); return SERIAL_LINE_OVERFLOW; }
            if (_length == 0) return SERIAL_LINE_NONE;
            _data[_length] = 0;
            _ready = true;
            return SERIAL_LINE_READY;
        }
        if (_ready) reset();
        if (_length + 1 < Capacity && !_overflow) _data[_length++] = character;
        else _overflow = true;
        return SERIAL_LINE_NONE;
    }

    const char* line() const { return _ready ? _data : ""; }
    void consume() { reset(); }
    bool overflowed() const { return _overflow; }

private:
    char _data[Capacity];
    size_t _length;
    bool _overflow;
    bool _ready;

    void reset() {
        _data[0] = 0;
        _length = 0;
        _overflow = false;
        _ready = false;
    }
};
