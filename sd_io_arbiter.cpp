#include "sd_io_arbiter.h"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

namespace {
StaticSemaphore_t s_mutexStorage;
SemaphoreHandle_t s_mutex = nullptr;
alignas(4) uint32_t s_acquisitions = 0;
alignas(4) uint32_t s_contentions = 0;
alignas(4) uint32_t s_maxWaitUs = 0;
alignas(4) uint32_t s_maxHoldUs = 0;

void updateMaximum(uint32_t* target, uint32_t candidate) {
    uint32_t current = __atomic_load_n(target, __ATOMIC_RELAXED);
    while (candidate > current &&
           !__atomic_compare_exchange_n(target, &current, candidate, false,
                                        __ATOMIC_RELAXED, __ATOMIC_RELAXED)) {}
}
}  // namespace

void sdIoInit() {
    if (!s_mutex) s_mutex = xSemaphoreCreateRecursiveMutexStatic(&s_mutexStorage);
    __atomic_store_n(&s_acquisitions, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_contentions, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_maxWaitUs, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_maxHoldUs, 0u, __ATOMIC_RELEASE);
}

SdIoGuard::SdIoGuard() : _locked(false), _lockedAtUs(0) {
    if (!s_mutex) return;
    const uint32_t started = micros();
    if (xSemaphoreTakeRecursive(s_mutex, 0) != pdTRUE) {
        __atomic_add_fetch(&s_contentions, 1u, __ATOMIC_RELAXED);
        if (xSemaphoreTakeRecursive(s_mutex, portMAX_DELAY) != pdTRUE) return;
    }
    const uint32_t acquired = micros();
    updateMaximum(&s_maxWaitUs, acquired - started);
    __atomic_add_fetch(&s_acquisitions, 1u, __ATOMIC_RELAXED);
    _lockedAtUs = acquired;
    _locked = true;
}

SdIoGuard::~SdIoGuard() {
    if (!_locked || !s_mutex) return;
    updateMaximum(&s_maxHoldUs, micros() - _lockedAtUs);
    xSemaphoreGiveRecursive(s_mutex);
}

SdIoSnapshot sdIoSnapshot() {
    return {
        s_mutex != nullptr,
        __atomic_load_n(&s_acquisitions, __ATOMIC_RELAXED),
        __atomic_load_n(&s_contentions, __ATOMIC_RELAXED),
        __atomic_load_n(&s_maxWaitUs, __ATOMIC_RELAXED),
        __atomic_load_n(&s_maxHoldUs, __ATOMIC_RELAXED),
    };
}
