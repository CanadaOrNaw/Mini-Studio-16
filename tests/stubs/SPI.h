#pragma once

#include <stdint.h>

class SPIClass {
public:
    void begin(uint8_t, uint8_t, uint8_t, uint8_t) {}
};

inline SPIClass SPI;
