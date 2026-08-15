#include "sampler_slots.h"

#include <string.h>

SamplerSlotBank g_samplerSlotBank;
SamplerSequence g_samplerSequence;

uint32_t SamplerSlotBank::normalizedFrames(uint32_t sourceFrames, uint32_t sourceRate) {
    if (sourceFrames == 0 || sourceRate == 0) return 0;
    const uint64_t scaled = static_cast<uint64_t>(sourceFrames) * SAMPLE_RATE;
    return static_cast<uint32_t>((scaled + sourceRate - 1u) / sourceRate);
}

bool SamplerSlotBank::validRegion(const SamplerSlot& slot, const SamplerRegion& region) {
    if (slot.mode == SAMPLER_SLOT_EMPTY || region.lengthFrames == 0) return false;
    const uint64_t regionEnd = static_cast<uint64_t>(region.startFrame) + region.lengthFrames;
    const uint64_t trimEnd = static_cast<uint64_t>(slot.trimStart) + slot.trimLength;
    return region.startFrame >= slot.trimStart && regionEnd <= trimEnd &&
           trimEnd <= slot.sourceFrames;
}

uint32_t SamplerSlotBank::slotGeneration(uint8_t index) const {
    return validSlot(index)
        ? __atomic_load_n(&_generations[index], __ATOMIC_ACQUIRE) : 0;
}

void SamplerSlotBank::beginEdit(uint8_t index) {
    if (validSlot(index))                       // generation becomes odd
        __atomic_add_fetch(&_generations[index], 1u, __ATOMIC_ACQ_REL);
}

void SamplerSlotBank::endEdit(uint8_t index) {
    if (validSlot(index))                       // generation becomes even
        __atomic_add_fetch(&_generations[index], 1u, __ATOMIC_RELEASE);
}

bool SamplerSlotBank::snapshotSlot(uint8_t index, SamplerSlot& out) const {
    if (!validSlot(index)) return false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        const uint32_t before =
            __atomic_load_n(&_generations[index], __ATOMIC_ACQUIRE);
        if (before & 1u) continue;              // writer in progress
        memcpy(&out, &_slots[index], sizeof(out));
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t after =
            __atomic_load_n(&_generations[index], __ATOMIC_ACQUIRE);
        if (before == after) return true;
    }
    return false;                               // persistent writer churn
}

void SamplerSlotBank::clear() {
    for (uint8_t index = 0; index < SAMPLER_SLOT_COUNT; ++index) beginEdit(index);
    memset(_slots, 0, sizeof(_slots));
    _quotaUsedFrames = 0;
    for (uint8_t index = 0; index < SAMPLER_SLOT_COUNT; ++index) endEdit(index);
}

bool SamplerSlotBank::assign(uint8_t index, const char* filename, uint32_t sourceFrames,
                             uint32_t sourceRate, SamplerSlotMode mode) {
    if (!validSlot(index) || !filename || !filename[0] || sourceFrames < SAMPLER_SLICE_COUNT ||
        (mode != SAMPLER_SLOT_MELODIC && mode != SAMPLER_SLOT_SLICED))
        return false;
    const size_t nameLength = strlen(filename);
    if (nameLength >= SAMPLE_NAME_LEN) return false;
    const uint32_t quota = normalizedFrames(sourceFrames, sourceRate);
    if (quota == 0 || quota > SAMPLER_QUOTA_FRAMES) return false;
    const uint32_t oldQuota = _slots[index].quotaFrames;
    const uint64_t nextQuota = static_cast<uint64_t>(_quotaUsedFrames) - oldQuota + quota;
    if (nextQuota > SAMPLER_QUOTA_FRAMES) return false;

    SamplerSlot replacement = {};
    replacement.mode = mode;
    memcpy(replacement.filename, filename, nameLength + 1);
    replacement.sourceFrames = sourceFrames;
    replacement.sourceRate = sourceRate;
    replacement.quotaFrames = quota;
    replacement.trimStart = 0;
    replacement.trimLength = sourceFrames;
    replacement.rootMidi = 60;
    replacement.pitchQ8 = 0;
    replacement.gainQ15 = 32767;
    replacement.cutoffQ15 = 32767;
    replacement.resonanceQ15 = 0;
    beginEdit(index);
    _slots[index] = replacement;
    _quotaUsedFrames = static_cast<uint32_t>(nextQuota);
    const bool sliced = autoSliceUnlocked(index);
    endEdit(index);
    return sliced;
}

bool SamplerSlotBank::remove(uint8_t index) {
    if (!validSlot(index)) return false;
    beginEdit(index);
    _quotaUsedFrames -= _slots[index].quotaFrames;
    memset(&_slots[index], 0, sizeof(_slots[index]));
    endEdit(index);
    return true;
}

bool SamplerSlotBank::copySlot(uint8_t source, uint8_t destination) {
    if (!validSlot(source) || !validSlot(destination) || source == destination ||
        _slots[source].mode == SAMPLER_SLOT_EMPTY)
        return false;
    const uint32_t oldQuota = _slots[destination].quotaFrames;
    const uint64_t nextQuota = static_cast<uint64_t>(_quotaUsedFrames) - oldQuota +
                               _slots[source].quotaFrames;
    if (nextQuota > SAMPLER_QUOTA_FRAMES) return false;
    // A2-P3 (alpha.2 reconciliation): read the source through its own
    // seqlock. The sampler worker writes rootMidi after a pitch-detected mic
    // capture, so a plain struct read here could tear.
    SamplerSlot copy;
    if (!snapshotSlot(source, copy)) return false;
    beginEdit(destination);
    _slots[destination] = copy;
    _quotaUsedFrames = static_cast<uint32_t>(nextQuota);
    endEdit(destination);
    return true;
}

bool SamplerSlotBank::copySlice(uint8_t sourceSlot, uint8_t sourceSlice,
                                uint8_t destinationSlot, uint8_t destinationSlice) {
    if (!validSlot(sourceSlot) || !validSlot(destinationSlot) ||
        sourceSlice >= SAMPLER_SLICE_COUNT || destinationSlice >= SAMPLER_SLICE_COUNT ||
        _slots[sourceSlot].mode == SAMPLER_SLOT_EMPTY ||
        _slots[destinationSlot].mode != SAMPLER_SLOT_SLICED)
        return false;
    const SamplerRegion source = _slots[sourceSlot].slices[sourceSlice];
    // Slice-copy is non-destructive metadata copying when both slices address
    // the same underlying asset. A different asset cannot be referenced by
    // SamplerRegion alone, so reject it instead of silently playing wrong PCM.
    if (strncmp(_slots[sourceSlot].filename, _slots[destinationSlot].filename,
                SAMPLE_NAME_LEN) != 0 || !validRegion(_slots[destinationSlot], source))
        return false;
    beginEdit(destinationSlot);
    _slots[destinationSlot].slices[destinationSlice] = source;
    endEdit(destinationSlot);
    return true;
}

bool SamplerSlotBank::setMode(uint8_t index, SamplerSlotMode mode) {
    if (!validSlot(index) || _slots[index].mode == SAMPLER_SLOT_EMPTY ||
        (mode != SAMPLER_SLOT_MELODIC && mode != SAMPLER_SLOT_SLICED))
        return false;
    beginEdit(index);
    _slots[index].mode = mode;
    const bool sliced = mode == SAMPLER_SLOT_SLICED
        ? autoSliceUnlocked(index) : true;
    endEdit(index);
    return sliced;
}

bool SamplerSlotBank::setTrim(uint8_t index, uint32_t startFrame, uint32_t lengthFrames) {
    if (!validSlot(index) || _slots[index].mode == SAMPLER_SLOT_EMPTY ||
        lengthFrames < SAMPLER_SLICE_COUNT ||
        static_cast<uint64_t>(startFrame) + lengthFrames >
                                 _slots[index].sourceFrames)
        return false;
    beginEdit(index);
    _slots[index].trimStart = startFrame;
    _slots[index].trimLength = lengthFrames;
    const bool sliced = autoSliceUnlocked(index);
    endEdit(index);
    return sliced;
}

bool SamplerSlotBank::setSlice(uint8_t index, uint8_t slice, uint32_t startFrame,
                               uint32_t lengthFrames) {
    if (!validSlot(index) || slice >= SAMPLER_SLICE_COUNT) return false;
    const SamplerRegion replacement = {startFrame, lengthFrames};
    if (!validRegion(_slots[index], replacement)) return false;
    beginEdit(index);
    _slots[index].slices[slice] = replacement;
    endEdit(index);
    return true;
}

bool SamplerSlotBank::autoSlice(uint8_t index) {
    if (!validSlot(index) || _slots[index].mode == SAMPLER_SLOT_EMPTY) return false;
    beginEdit(index);
    const bool sliced = autoSliceUnlocked(index);
    endEdit(index);
    return sliced;
}

bool SamplerSlotBank::autoSliceUnlocked(uint8_t index) {
    // Callers that already hold the slot seqlock use this helper. Incrementing
    // the seqlock inside an outer edit would make its generation even and
    // briefly expose a half-written slot to snapshotSlot().
    SamplerSlot& item = _slots[index];
    const uint32_t base = item.trimLength / SAMPLER_SLICE_COUNT;
    const uint32_t remainder = item.trimLength % SAMPLER_SLICE_COUNT;
    uint32_t cursor = item.trimStart;
    for (uint8_t slice = 0; slice < SAMPLER_SLICE_COUNT; ++slice) {
        const uint32_t length = base + (slice < remainder ? 1u : 0u);
        item.slices[slice].startFrame = cursor;
        item.slices[slice].lengthFrames = length;
        cursor += length;
    }
    return true;
}

bool SamplerSlotBank::region(uint8_t index, uint8_t key, SamplerRegion& result) const {
    result = {0, 0};
    if (!validSlot(index) || key >= SAMPLER_SLICE_COUNT) return false;
    const SamplerSlot& item = _slots[index];
    if (item.mode == SAMPLER_SLOT_EMPTY) return false;
    result = item.mode == SAMPLER_SLOT_SLICED
        ? item.slices[key] : SamplerRegion{item.trimStart, item.trimLength};
    return validRegion(item, result);
}

bool SamplerSlotBank::validate() const {
    uint64_t quota = 0;
    for (uint8_t index = 0; index < SAMPLER_SLOT_COUNT; ++index) {
        const SamplerSlot& item = _slots[index];
        if (item.mode == SAMPLER_SLOT_EMPTY) {
            if (item.quotaFrames != 0) return false;
            continue;
        }
        if (item.mode != SAMPLER_SLOT_MELODIC && item.mode != SAMPLER_SLOT_SLICED)
            return false;
        if (!item.filename[0] || item.filename[SAMPLE_NAME_LEN - 1] != 0 ||
            item.sourceFrames == 0 || item.sourceRate == 0 || item.quotaFrames == 0 ||
            item.trimLength == 0 ||
            static_cast<uint64_t>(item.trimStart) + item.trimLength > item.sourceFrames)
            return false;
        quota += item.quotaFrames;
        for (uint8_t slice = 0; slice < SAMPLER_SLICE_COUNT; ++slice)
            if (!validRegion(item, item.slices[slice])) return false;
    }
    return quota == _quotaUsedFrames && quota <= SAMPLER_QUOTA_FRAMES;
}

void SamplerSequence::clear() {
    memset(_keys, 0xFF, sizeof(_keys));
    memset(_locks, 0, sizeof(_locks));
    _lockCount = 0;
}

void SamplerSequence::clearPattern(uint8_t pattern) {
    if (pattern >= NUM_PATTERNS) return;
    memset(_keys[pattern], 0xFF, sizeof(_keys[pattern]));
    for (uint16_t index = 0; index < _lockCount;) {
        if (_locks[index].pattern == pattern) {
            _locks[index] = _locks[--_lockCount];
        } else {
            ++index;
        }
    }
}

bool SamplerSequence::setTrigger(uint8_t pattern, uint8_t step, uint8_t slot,
                                 bool enabled) {
    return enabled ? setEvent(pattern, step, slot, 0) : clearEvent(pattern, step, slot);
}

bool SamplerSequence::setEvent(uint8_t pattern, uint8_t step, uint8_t slot, uint8_t key) {
    if (pattern >= NUM_PATTERNS || step >= NUM_STEPS || slot >= SAMPLER_SLOT_COUNT)
        return false;
    if (key >= SAMPLER_SLICE_COUNT) return false;
    _keys[pattern][step][slot] = key;
    return true;
}

bool SamplerSequence::clearEvent(uint8_t pattern, uint8_t step, uint8_t slot) {
    if (pattern >= NUM_PATTERNS || step >= NUM_STEPS || slot >= SAMPLER_SLOT_COUNT)
        return false;
    _keys[pattern][step][slot] = 0xFF;
    return true;
}

uint8_t SamplerSequence::eventKey(uint8_t pattern, uint8_t step, uint8_t slot) const {
    return pattern < NUM_PATTERNS && step < NUM_STEPS && slot < SAMPLER_SLOT_COUNT
        ? _keys[pattern][step][slot] : 0xFF;
}

uint16_t SamplerSequence::triggers(uint8_t pattern, uint8_t step) const {
    if (pattern >= NUM_PATTERNS || step >= NUM_STEPS) return 0;
    uint16_t result = 0;
    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot)
        if (_keys[pattern][step][slot] != 0xFF) result |= static_cast<uint16_t>(1u << slot);
    return result;
}

const SamplerLockEntry* SamplerSequence::findLock(uint8_t pattern, uint8_t step,
                                                   uint8_t slot) const {
    for (uint16_t index = 0; index < _lockCount; ++index)
        if (_locks[index].pattern == pattern && _locks[index].step == step &&
            _locks[index].slot == slot)
            return &_locks[index];
    return nullptr;
}

bool SamplerSequence::removeLock(uint8_t pattern, uint8_t step, uint8_t slot) {
    for (uint16_t index = 0; index < _lockCount; ++index) {
        if (_locks[index].pattern != pattern || _locks[index].step != step ||
            _locks[index].slot != slot) continue;
        _locks[index] = _locks[--_lockCount];
        memset(&_locks[_lockCount], 0, sizeof(_locks[_lockCount]));
        return true;
    }
    return false;
}

bool SamplerSequence::setLock(const SamplerLockEntry& entry) {
    if (entry.pattern >= NUM_PATTERNS || entry.step >= NUM_STEPS ||
        entry.slot >= SAMPLER_SLOT_COUNT)
        return false;
    if (entry.flags == 0) return removeLock(entry.pattern, entry.step, entry.slot);
    for (uint16_t index = 0; index < _lockCount; ++index) {
        if (_locks[index].pattern == entry.pattern && _locks[index].step == entry.step &&
            _locks[index].slot == entry.slot) {
            _locks[index] = entry;
            return true;
        }
    }
    if (_lockCount >= SAMPLER_LOCK_CAPACITY) return false;
    _locks[_lockCount++] = entry;
    return true;
}

bool SamplerSequence::validate() const {
    if (_lockCount > SAMPLER_LOCK_CAPACITY) return false;
    for (uint8_t pattern = 0; pattern < NUM_PATTERNS; ++pattern)
        for (uint8_t step = 0; step < NUM_STEPS; ++step)
            for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot)
                if (_keys[pattern][step][slot] != 0xFF &&
                    _keys[pattern][step][slot] >= SAMPLER_SLICE_COUNT)
                    return false;
    for (uint16_t index = 0; index < _lockCount; ++index) {
        const SamplerLockEntry& item = _locks[index];
        if (item.pattern >= NUM_PATTERNS || item.step >= NUM_STEPS ||
            item.slot >= SAMPLER_SLOT_COUNT || item.flags == 0)
            return false;
        for (uint16_t other = index + 1; other < _lockCount; ++other)
            if (_locks[other].pattern == item.pattern && _locks[other].step == item.step &&
                _locks[other].slot == item.slot)
                return false;
    }
    return true;
}
