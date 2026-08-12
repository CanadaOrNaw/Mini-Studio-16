#include "../sampler_slots.h"

#include <cassert>
#include <cstdint>
#include <iostream>

int main() {
    SamplerSlotBank bank;
    const uint32_t beforeAssign = bank.slotGeneration(0);
    assert(bank.assign(0, "voice.wav", 22050, 22050, SAMPLER_SLOT_MELODIC));
    assert(bank.slotGeneration(0) == beforeAssign + 2);
    assert(bank.quotaUsedFrames() == 22050);
    SamplerRegion region = {};
    assert(bank.region(0, 12, region));
    assert(region.startFrame == 0 && region.lengthFrames == 22050);

    uint32_t generation = bank.slotGeneration(0);
    assert(bank.setTrim(0, 50, 16003));
    assert(bank.slotGeneration(0) == generation + 2);
    generation = bank.slotGeneration(0);
    assert(bank.setMode(0, SAMPLER_SLOT_SLICED));
    assert(bank.slotGeneration(0) == generation + 2);
    uint32_t total = 0;
    for (uint8_t slice = 0; slice < 16; ++slice) {
        assert(bank.region(0, slice, region));
        total += region.lengthFrames;
        if (slice) {
            SamplerRegion previous = {};
            assert(bank.region(0, slice - 1, previous));
            assert(previous.startFrame + previous.lengthFrames == region.startFrame);
        }
    }
    assert(total == 16003);
    assert(bank.setSlice(0, 3, 100, 200));
    assert(!bank.setSlice(0, 3, 0, 200));
    assert(bank.copySlot(0, 2));
    assert(bank.slot(2).mode == SAMPLER_SLOT_SLICED);
    assert(bank.quotaUsedFrames() == 44100);
    assert(bank.copySlice(0, 3, 2, 4));
    assert(bank.slot(2).slices[4].startFrame == 100);
    assert(!bank.copySlot(0, 0));
    assert(bank.validate());

    // Quota is duration-normalized, so 48 kHz source material consumes the
    // same project time as 22.05 kHz material.
    assert(bank.assign(1, "two.wav", 48000, 48000, SAMPLER_SLOT_MELODIC));
    assert(bank.quotaUsedFrames() == 66150);
    assert(!bank.assign(2, "too-long.wav", 40 * 48000, 48000,
                        SAMPLER_SLOT_MELODIC));
    assert(bank.remove(1));
    assert(bank.quotaUsedFrames() == 44100);

    SamplerSequence sequence;
    assert(sequence.setTrigger(15, 15, 15, true));
    assert(sequence.triggers(15, 15) == 0x8000);
    assert(sequence.setEvent(15, 15, 15, 12));
    assert(sequence.eventKey(15, 15, 15) == 12);
    SamplerLockEntry lock = {};
    lock.pattern = 15;
    lock.step = 15;
    lock.slot = 15;
    lock.flags = SAMPLER_LOCK_PITCH | SAMPLER_LOCK_FILTER;
    lock.pitchQ8 = -12 * 256;
    lock.cutoffQ15 = 12000;
    lock.resonanceQ15 = 8000;
    assert(sequence.setLock(lock));
    assert(sequence.lockCount() == 1);
    assert(sequence.findLock(15, 15, 15)->pitchQ8 == -3072);
    lock.pitchQ8 = 7 * 256;
    assert(sequence.setLock(lock));
    assert(sequence.lockCount() == 1);
    assert(sequence.findLock(15, 15, 15)->pitchQ8 == 1792);
    assert(sequence.validate());
    sequence.clearPattern(15);
    assert(sequence.triggers(15, 15) == 0 && sequence.lockCount() == 0);

    // Fill every sparse lock entry and prove the next distinct key is bounded.
    for (uint16_t index = 0; index < SAMPLER_LOCK_CAPACITY; ++index) {
        SamplerLockEntry item = {};
        item.pattern = static_cast<uint8_t>(index / (NUM_STEPS * 8));
        item.step = static_cast<uint8_t>((index / 8) % NUM_STEPS);
        item.slot = static_cast<uint8_t>(index % 8);
        item.flags = SAMPLER_LOCK_GAIN;
        item.gainQ15 = 20000;
        assert(sequence.setLock(item));
    }
    SamplerLockEntry extra = {};
    extra.pattern = 1;
    extra.step = 0;
    extra.slot = 8;
    extra.flags = SAMPLER_LOCK_GAIN;
    assert(!sequence.setLock(extra));
    assert(sequence.validate());

    std::cout << "sampler_slots: quota, slicing and sparse locks passed\n";
    return 0;
}
