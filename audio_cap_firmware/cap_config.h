#pragma once

// Mini Studio Audio Cap Rev A / original ESP32-WROOM-32E pin assignment.
// These are cap-side GPIO numbers, not Cardputer GPIO numbers.
#define CAP_SPI_MISO       12
#define CAP_SPI_MOSI       13
#define CAP_SPI_SCLK       14
#define CAP_SPI_CS         15
#define CAP_HOST_IRQ        4

#define CAP_I2S_LRCK       25
#define CAP_I2S_BCLK       26
#define CAP_I2S_ADC_DATA   35

#define CAP_PAIR_BUTTON    32
#define CAP_STATUS_LED     33

#define CAP_A2DP_RATE   44100
#define CAP_I2S_DMA_FRAMES 256
