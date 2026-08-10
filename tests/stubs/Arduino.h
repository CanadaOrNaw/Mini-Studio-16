#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

using TaskHandle_t = void*;
using BaseType_t = int;
using portMUX_TYPE = int;
using SemaphoreHandle_t = void*;
struct StaticSemaphore_t { int value; };

#define portMUX_INITIALIZER_UNLOCKED 0
#define pdPASS 1
#define pdTRUE 1
#define portMAX_DELAY 0xFFFFFFFFu
#define portENTER_CRITICAL(mux) ((void)(mux))
#define portEXIT_CRITICAL(mux) ((void)(mux))
#define taskYIELD() ((void)0)

inline uint32_t micros() { static uint32_t value = 0; return ++value; }
inline uint32_t millis() { static uint32_t value = 0; return ++value; }
inline void delay(uint32_t) {}
inline void vTaskDelete(void*) {}
inline void vTaskDelay(uint32_t) {}
inline SemaphoreHandle_t xSemaphoreCreateRecursiveMutexStatic(StaticSemaphore_t* storage) {
    return storage;
}
inline BaseType_t xSemaphoreTakeRecursive(SemaphoreHandle_t, uint32_t) { return pdTRUE; }
inline BaseType_t xSemaphoreGiveRecursive(SemaphoreHandle_t) { return pdTRUE; }
template <typename T>
inline T constrain(T value, T minimum, T maximum) {
    return value < minimum ? minimum : (value > maximum ? maximum : value);
}
template <typename T>
inline T min(T a, T b) { return a < b ? a : b; }
template <typename T>
inline T max(T a, T b) { return a > b ? a : b; }
inline BaseType_t xTaskCreatePinnedToCore(
    void (*)(void*), const char*, uint32_t, void*, uint32_t, TaskHandle_t*, uint32_t) {
    return pdPASS;
}

struct SerialStub {
    void begin(uint32_t) {}
    int available() const { return 0; }
    int read() { return -1; }
    void println(const char*) {}
    template <typename... Args>
    void printf(const char*, Args...) {}
};
static SerialStub Serial;

class String {
public:
    String() = default;
    String(const char* value) : value_(value ? value : "") {}
    int lastIndexOf(char needle) const {
        const auto position = value_.find_last_of(needle);
        return position == std::string::npos ? -1 : static_cast<int>(position);
    }
    String substring(size_t start) const { return String(value_.substr(start).c_str()); }
    String substring(size_t start, size_t end) const {
        return String(value_.substr(start, end - start).c_str());
    }
    bool endsWith(const char* suffix) const {
        const std::string ending = suffix ? suffix : "";
        return value_.size() >= ending.size() &&
               value_.compare(value_.size() - ending.size(), ending.size(), ending) == 0;
    }
    const char* c_str() const { return value_.c_str(); }
private:
    std::string value_;
};
