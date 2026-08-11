#pragma once

#include <stdint.h>
#include <stddef.h>

#define HSPI 1
#define MSBFIRST 1
#define SPI_MODE0 0

class SPISettings {
public:
    SPISettings(uint32_t, uint8_t, uint8_t) {}
};

class SPIClass {
public:
    explicit SPIClass(uint8_t = 0) {}
    void begin(uint8_t, uint8_t, uint8_t, uint8_t) {}
    void beginTransaction(const SPISettings&) {}
    void endTransaction() {}
    void transferBytes(const uint8_t*, uint8_t* receive, uint32_t count) {
        if (receive) for (uint32_t i = 0; i < count; ++i) receive[i] = 0;
    }
};

static SPIClass SPI;
