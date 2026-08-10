// Lock-free single-producer/single-consumer ring used between the audio and
// storage tasks. The producer is the only writer of _write; the consumer is
// the only writer of _read. Capacity must be a power of two.
#pragma once

#include <stddef.h>
#include <stdint.h>

template <typename T, uint32_t Capacity>
class SpscRing {
    static_assert(Capacity >= 2, "ring capacity must be at least two elements");
    static_assert((Capacity & (Capacity - 1)) == 0, "ring capacity must be a power of two");

public:
    static constexpr uint32_t capacity() { return Capacity; }

    SpscRing() : _read(0), _write(0) {}

    void reset() {
        __atomic_store_n(&_read, 0u, __ATOMIC_RELEASE);
        __atomic_store_n(&_write, 0u, __ATOMIC_RELEASE);
    }

    uint32_t size() const {
        const uint32_t write = __atomic_load_n(&_write, __ATOMIC_ACQUIRE);
        const uint32_t read = __atomic_load_n(&_read, __ATOMIC_ACQUIRE);
        return write - read;
    }

    uint32_t freeSpace() const { return Capacity - size(); }
    bool empty() const { return size() == 0; }
    bool full() const { return size() == Capacity; }

    bool pushOne(const T& value) { return push(&value, 1) == 1; }
    bool popOne(T& value) { return pop(&value, 1) == 1; }

    bool peek(size_t offset, T& value) const {
        const uint32_t read = __atomic_load_n(&_read, __ATOMIC_RELAXED);
        const uint32_t write = __atomic_load_n(&_write, __ATOMIC_ACQUIRE);
        if (offset >= static_cast<size_t>(write - read)) return false;
        value = _data[(read + static_cast<uint32_t>(offset)) & (Capacity - 1)];
        return true;
    }

    size_t push(const T* source, size_t count) {
        if (!source || count == 0) return 0;

        const uint32_t write = __atomic_load_n(&_write, __ATOMIC_RELAXED);
        const uint32_t read = __atomic_load_n(&_read, __ATOMIC_ACQUIRE);
        uint32_t available = Capacity - (write - read);
        if (count > available) count = available;

        for (size_t i = 0; i < count; ++i)
            _data[(write + static_cast<uint32_t>(i)) & (Capacity - 1)] = source[i];

        __atomic_store_n(&_write, write + static_cast<uint32_t>(count), __ATOMIC_RELEASE);
        return count;
    }

    size_t pop(T* destination, size_t count) {
        if (!destination || count == 0) return 0;

        const uint32_t read = __atomic_load_n(&_read, __ATOMIC_RELAXED);
        const uint32_t write = __atomic_load_n(&_write, __ATOMIC_ACQUIRE);
        uint32_t available = write - read;
        if (count > available) count = available;

        for (size_t i = 0; i < count; ++i)
            destination[i] = _data[(read + static_cast<uint32_t>(i)) & (Capacity - 1)];

        __atomic_store_n(&_read, read + static_cast<uint32_t>(count), __ATOMIC_RELEASE);
        return count;
    }

    size_t discard(size_t count) {
        const uint32_t read = __atomic_load_n(&_read, __ATOMIC_RELAXED);
        const uint32_t write = __atomic_load_n(&_write, __ATOMIC_ACQUIRE);
        uint32_t available = write - read;
        if (count > available) count = available;
        __atomic_store_n(&_read, read + static_cast<uint32_t>(count), __ATOMIC_RELEASE);
        return count;
    }

private:
    alignas(4) T _data[Capacity];
    alignas(4) uint32_t _read;
    alignas(4) uint32_t _write;
};
