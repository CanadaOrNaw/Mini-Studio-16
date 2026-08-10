#pragma once

#include "config.h"

#include <stddef.h>
#include <stdint.h>

static const uint8_t SAMPLER_SLOT_COUNT = 16;
static const uint8_t SAMPLER_SLICE_COUNT = 16;
static const uint16_t SAMPLER_LOCK_CAPACITY = 128;
static const uint32_t SAMPLER_QUOTA_FRAMES = SAMPLE_RATE * 40UL;

enum SamplerSlotMode : uint8_t {
    SAMPLER_SLOT_EMPTY = 0,
    SAMPLER_SLOT_MELODIC,
    SAMPLER_SLOT_SLICED,
};

enum SamplerLockFlags : uint8_t {
    SAMPLER_LOCK_PITCH = 1u << 0,
    SAMPLER_LOCK_GAIN = 1u << 1,
    SAMPLER_LOCK_FILTER = 1u << 2,
    SAMPLER_LOCK_TRIM = 1u << 3,
};

struct SamplerRegion {
    uint32_t startFrame;
    uint32_t lengthFrames;
};

struct SamplerSlot {
    uint8_t mode;
    char filename[SAMPLE_NAME_LEN];
    uint32_t sourceFrames;
    uint32_t sourceRate;
    uint32_t quotaFrames;
    uint32_t trimStart;
    uint32_t trimLength;
    uint8_t rootMidi;
    int16_t pitchQ8;
    uint16_t gainQ15;
    uint16_t cutoffQ15;
    uint16_t resonanceQ15;
    SamplerRegion slices[SAMPLER_SLICE_COUNT];
};

struct SamplerLockEntry {
    uint8_t pattern;
    uint8_t step;
    uint8_t slot;
    uint8_t flags;
    int16_t pitchQ8;
    uint16_t gainQ15;
    uint16_t cutoffQ15;
    uint16_t resonanceQ15;
    uint16_t trimStartQ15;
    uint16_t trimLengthQ15;
};

class SamplerSlotBank {
public:
    SamplerSlotBank() { clear(); }
    void clear();
    bool assign(uint8_t slot, const char* filename, uint32_t sourceFrames,
                uint32_t sourceRate, SamplerSlotMode mode);
    bool remove(uint8_t slot);
    bool setMode(uint8_t slot, SamplerSlotMode mode);
    bool setTrim(uint8_t slot, uint32_t startFrame, uint32_t lengthFrames);
    bool setSlice(uint8_t slot, uint8_t slice, uint32_t startFrame,
                  uint32_t lengthFrames);
    bool autoSlice(uint8_t slot);
    bool region(uint8_t slot, uint8_t key, SamplerRegion& result) const;
    bool validate() const;
    uint32_t quotaUsedFrames() const { return _quotaUsedFrames; }
    uint32_t quotaRemainingFrames() const {
        return _quotaUsedFrames >= SAMPLER_QUOTA_FRAMES
            ? 0 : SAMPLER_QUOTA_FRAMES - _quotaUsedFrames;
    }
    const SamplerSlot& slot(uint8_t index) const { return _slots[index]; }
    SamplerSlot& slot(uint8_t index) { return _slots[index]; }

private:
    SamplerSlot _slots[SAMPLER_SLOT_COUNT];
    uint32_t _quotaUsedFrames;

    static uint32_t normalizedFrames(uint32_t sourceFrames, uint32_t sourceRate);
    static bool validSlot(uint8_t slot) { return slot < SAMPLER_SLOT_COUNT; }
    static bool validRegion(const SamplerSlot& slot, const SamplerRegion& region);
};

class SamplerSequence {
public:
    SamplerSequence() { clear(); }
    void clear();
    void clearPattern(uint8_t pattern);
    bool setTrigger(uint8_t pattern, uint8_t step, uint8_t slot, bool enabled);
    bool setEvent(uint8_t pattern, uint8_t step, uint8_t slot, uint8_t key);
    bool clearEvent(uint8_t pattern, uint8_t step, uint8_t slot);
    uint8_t eventKey(uint8_t pattern, uint8_t step, uint8_t slot) const;
    uint16_t triggers(uint8_t pattern, uint8_t step) const;
    bool setLock(const SamplerLockEntry& entry);
    bool removeLock(uint8_t pattern, uint8_t step, uint8_t slot);
    const SamplerLockEntry* findLock(uint8_t pattern, uint8_t step, uint8_t slot) const;
    uint16_t lockCount() const { return _lockCount; }
    const SamplerLockEntry& lock(uint16_t index) const { return _locks[index]; }
    bool validate() const;

private:
    uint8_t _keys[NUM_PATTERNS][NUM_STEPS][SAMPLER_SLOT_COUNT];
    SamplerLockEntry _locks[SAMPLER_LOCK_CAPACITY];
    uint16_t _lockCount;
};

extern SamplerSlotBank g_samplerSlotBank;
extern SamplerSequence g_samplerSequence;
