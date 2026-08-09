#pragma once

#include "Arduino.h"
#include <stddef.h>
#include <stdint.h>

#define FILE_READ 0
#define FILE_WRITE 1

class File {
public:
    explicit operator bool() const { return true; }
    size_t write(const uint8_t*, size_t length) { return length; }
    int read(uint8_t*, size_t length) { return static_cast<int>(length); }
    bool seek(uint32_t) { return true; }
    int available() const { return 0; }
    uint32_t position() const { return 0; }
    size_t size() const { return 0; }
    const char* name() const { return "test.wav"; }
    bool isDirectory() const { return false; }
    File openNextFile() { return File(); }
    void flush() {}
    void close() {}
};

class SDStub {
public:
    template <typename Spi>
    bool begin(uint8_t, Spi&, uint32_t) { return true; }
    bool exists(const char*) const { return true; }
    bool mkdir(const char*) { return true; }
    bool rmdir(const char*) { return true; }
    bool remove(const char*) { return true; }
    bool rename(const char*, const char*) { return true; }
    File open(const char*, uint8_t) { return File(); }
    File open(const char*) { return File(); }
};
static SDStub SD;
