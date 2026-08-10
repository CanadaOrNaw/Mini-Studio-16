#pragma once

#include "Arduino.h"
#include <stdint.h>
#include <vector>

#define TFT_BLACK 0

class Keyboard_Class {
public:
    struct KeysState {
        bool fn = false;
        bool shift = false;
        bool ctrl = false;
        bool opt = false;
        bool alt = false;
        bool tab = false;
        bool del = false;
        bool enter = false;
        bool space = false;
        std::vector<char> word;
    };
};

struct KeyboardStub {
    bool isChange() const { return false; }
    bool isPressed() const { return false; }
    Keyboard_Class::KeysState keysState() const { return {}; }
};

struct ButtonStub { bool wasPressed() const { return false; } };
struct DisplayStub {
    void setRotation(int) {}
    void fillScreen(uint32_t) {}
};

struct SpeakerConfigStub {
    uint32_t sample_rate = 0;
    uint8_t task_priority = 0;
    size_t dma_buf_count = 0;
    size_t dma_buf_len = 0;
};

struct SpeakerStub {
    SpeakerConfigStub config() const { return {}; }
    void config(const SpeakerConfigStub&) {}
    bool begin() { return true; }
    void end() {}
    void setVolume(uint8_t) {}
    bool playRaw(const int16_t*, size_t, uint32_t, bool, uint32_t, int) { return true; }
};

struct MicStub {
    bool begin() { return true; }
    void end() {}
    bool record(int16_t*, size_t, uint32_t) { return true; }
    size_t isRecording() const { return 0; }
};

struct CardputerStub {
    KeyboardStub Keyboard;
    ButtonStub BtnA;
    DisplayStub Display;
    SpeakerStub Speaker;
    MicStub Mic;
    template <typename Config>
    void begin(const Config&, bool) {}
    void update() {}
};

static CardputerStub M5Cardputer;

namespace m5 {
struct imu_3d_t { float x = 0, y = 0, z = 0; };
struct imu_data_t { imu_3d_t accel; imu_3d_t gyro; imu_3d_t mag; };
}

struct ImuStub {
    bool isEnabled() const { return true; }
    bool update() { return false; }
    m5::imu_data_t getImuData() const { return {}; }
};

struct M5Stub {
    struct Config {};
    ImuStub Imu;
    Config config() const { return {}; }
};
static M5Stub M5;

class M5Canvas {
public:
    explicit M5Canvas(DisplayStub*) {}
    void createSprite(int, int) {}
    void setTextFont(int) {}
    void setTextSize(int) {}
    void setTextColor(uint16_t) {}
    void setCursor(int, int) {}
    void fillSprite(uint16_t) {}
    void fillRect(int, int, int, int, uint16_t) {}
    void drawRect(int, int, int, int, uint16_t) {}
    void fillCircle(int, int, int, uint16_t) {}
    void drawFastHLine(int, int, int, uint16_t) {}
    void drawFastVLine(int, int, int, uint16_t) {}
    void drawLine(int, int, int, int, uint16_t) {}
    void pushSprite(int, int) {}
    void print(const char*) {}
    void print(const String&) {}
    void print(char) {}
    void print(unsigned int) {}
    void print(int) {}
    template <typename... Args>
    void printf(const char*, Args...) {}
};
