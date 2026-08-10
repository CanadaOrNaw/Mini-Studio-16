#pragma once

#include "pcm_ring.h"

#include <stddef.h>
#include <stdint.h>

enum SampleStreamState : uint8_t {
    SAMPLE_STREAM_IDLE = 0,
    SAMPLE_STREAM_PREPARING,
    SAMPLE_STREAM_PLAYING,
    SAMPLE_STREAM_COMPLETE,
    SAMPLE_STREAM_UNDERRUN,
    SAMPLE_STREAM_ERROR,
};

struct SampleStreamVoiceSnapshot {
    SampleStreamState state;
    uint8_t slot;
    uint32_t regionFrames;
    uint32_t consumedFrames;
    uint32_t bufferedFrames;
    uint32_t underruns;
    uint32_t generation;
    bool eof;
};

template <uint8_t VoiceCount, uint32_t RingFrames>
class SampleStreamCore {
public:
    SampleStreamCore() { reset(); }

    void reset() {
        store(&_generation, 0);
        for (uint8_t index = 0; index < VoiceCount; ++index) {
            Voice& voice = _voices[index];
            voice.ring.reset();
            store(&voice.state, SAMPLE_STREAM_IDLE);
            store(&voice.slot, 0);
            store(&voice.regionFrames, 0);
            store(&voice.consumedFrames, 0);
            store(&voice.incrementQ16, 65536);
            store(&voice.fractionQ16, 0);
            store(&voice.gainQ15, 32767);
            store(&voice.cutoffQ15, 32767);
            store(&voice.resonanceQ15, 0);
            store(&voice.underruns, 0);
            store(&voice.generation, 0);
            store(&voice.eof, 0);
            voice.filterLow = 0.0f;
            voice.filterBand = 0.0f;
        }
    }

    uint8_t allocateVoice() const {
        uint8_t oldest = 0;
        uint32_t oldestGeneration = 0xFFFFFFFFu;
        for (uint8_t index = 0; index < VoiceCount; ++index) {
            const SampleStreamState state = voiceState(index);
            if (state == SAMPLE_STREAM_IDLE || state == SAMPLE_STREAM_COMPLETE ||
                state == SAMPLE_STREAM_UNDERRUN || state == SAMPLE_STREAM_ERROR)
                return index;
            const uint32_t generation = load(&_voices[index].generation);
            if (generation < oldestGeneration) {
                oldestGeneration = generation;
                oldest = index;
            }
        }
        return oldest;
    }

    bool prepare(uint8_t voiceIndex, uint8_t slot, uint32_t regionFrames,
                 uint32_t incrementQ16, uint16_t gainQ15,
                 uint16_t cutoffQ15 = 32767, uint16_t resonanceQ15 = 0) {
        if (voiceIndex >= VoiceCount || regionFrames < 2 || incrementQ16 == 0 ||
            gainQ15 > 32767 || cutoffQ15 > 32767 || resonanceQ15 > 32767) return false;
        Voice& voice = _voices[voiceIndex];
        store(&voice.state, SAMPLE_STREAM_PREPARING);
        voice.ring.reset();
        store(&voice.slot, slot);
        store(&voice.regionFrames, regionFrames);
        store(&voice.consumedFrames, 0);
        store(&voice.incrementQ16, incrementQ16);
        store(&voice.fractionQ16, 0);
        store(&voice.gainQ15, gainQ15);
        store(&voice.cutoffQ15, cutoffQ15);
        store(&voice.resonanceQ15, resonanceQ15);
        store(&voice.underruns, 0);
        store(&voice.eof, 0);
        voice.filterLow = 0.0f;
        voice.filterBand = 0.0f;
        store(&voice.generation, add(&_generation, 1));
        return true;
    }

    size_t push(uint8_t voiceIndex, const int16_t* frames, size_t count) {
        if (voiceIndex >= VoiceCount) return 0;
        const SampleStreamState state = voiceState(voiceIndex);
        if (state != SAMPLE_STREAM_PREPARING && state != SAMPLE_STREAM_PLAYING)
            return 0;
        return _voices[voiceIndex].ring.push(frames, count);
    }

    uint32_t freeSpace(uint8_t voiceIndex) const {
        return voiceIndex < VoiceCount ? _voices[voiceIndex].ring.freeSpace() : 0;
    }

    bool arm(uint8_t voiceIndex, uint32_t minimumFrames) {
        if (voiceIndex >= VoiceCount || voiceState(voiceIndex) != SAMPLE_STREAM_PREPARING ||
            _voices[voiceIndex].ring.size() < minimumFrames)
            return false;
        store(&_voices[voiceIndex].state, SAMPLE_STREAM_PLAYING);
        return true;
    }

    void markEof(uint8_t voiceIndex) {
        if (voiceIndex < VoiceCount) store(&_voices[voiceIndex].eof, 1);
    }

    void stop(uint8_t voiceIndex) {
        if (voiceIndex < VoiceCount) store(&_voices[voiceIndex].state, SAMPLE_STREAM_COMPLETE);
    }

    void markError(uint8_t voiceIndex) {
        if (voiceIndex < VoiceCount) store(&_voices[voiceIndex].state, SAMPLE_STREAM_ERROR);
    }

    int32_t render() {
        int32_t mix = 0;
        for (uint8_t index = 0; index < VoiceCount; ++index) {
            Voice& voice = _voices[index];
            if (voiceState(index) != SAMPLE_STREAM_PLAYING) continue;
            int16_t first = 0;
            int16_t second = 0;
            if (!voice.ring.peek(0, first)) {
                if (load(&voice.eof)) store(&voice.state, SAMPLE_STREAM_COMPLETE);
                else {
                    add(&voice.underruns, 1);
                    store(&voice.state, SAMPLE_STREAM_UNDERRUN);
                }
                continue;
            }
            if (!voice.ring.peek(1, second)) {
                if (!load(&voice.eof)) {
                    add(&voice.underruns, 1);
                    store(&voice.state, SAMPLE_STREAM_UNDERRUN);
                    continue;
                }
                second = first;
            }

            const uint32_t fraction = load(&voice.fractionQ16);
            const int32_t interpolated = static_cast<int32_t>(first) +
                ((static_cast<int32_t>(second) - first) * static_cast<int32_t>(fraction) >> 16);
            int32_t filtered = interpolated;
            const uint32_t cutoff = load(&voice.cutoffQ15);
            const uint32_t resonance = load(&voice.resonanceQ15);
            if (cutoff < 32767 || resonance != 0) {
                const float input = static_cast<float>(interpolated) * (1.0f / 32768.0f);
                const float frequency = 0.02f + 0.80f *
                    (static_cast<float>(cutoff) / 32767.0f);
                const float damping = 1.0f - 0.90f *
                    (static_cast<float>(resonance) / 32767.0f);
                voice.filterLow += frequency * voice.filterBand;
                voice.filterBand += frequency *
                    (input - voice.filterLow - damping * voice.filterBand);
                if (voice.filterLow > 1.0f) voice.filterLow = 1.0f;
                else if (voice.filterLow < -1.0f) voice.filterLow = -1.0f;
                filtered = static_cast<int32_t>(voice.filterLow * 32767.0f);
            }
            mix += filtered * static_cast<int32_t>(load(&voice.gainQ15)) / 32767;

            const uint32_t phase = fraction + load(&voice.incrementQ16);
            const uint32_t discard = phase >> 16;
            store(&voice.fractionQ16, phase & 0xFFFFu);
            const size_t discarded = voice.ring.discard(discard);
            const uint32_t consumed = add(&voice.consumedFrames,
                                           static_cast<uint32_t>(discarded));
            if (discarded != discard && !load(&voice.eof)) {
                add(&voice.underruns, 1);
                store(&voice.state, SAMPLE_STREAM_UNDERRUN);
            } else if (consumed >= load(&voice.regionFrames)) {
                store(&voice.state, SAMPLE_STREAM_COMPLETE);
            }
        }
        return mix;
    }

    SampleStreamState voiceState(uint8_t voiceIndex) const {
        return voiceIndex < VoiceCount
            ? static_cast<SampleStreamState>(load(&_voices[voiceIndex].state))
            : SAMPLE_STREAM_ERROR;
    }

    SampleStreamVoiceSnapshot snapshot(uint8_t voiceIndex) const {
        SampleStreamVoiceSnapshot result = {};
        if (voiceIndex >= VoiceCount) { result.state = SAMPLE_STREAM_ERROR; return result; }
        const Voice& voice = _voices[voiceIndex];
        result.state = static_cast<SampleStreamState>(load(&voice.state));
        result.slot = static_cast<uint8_t>(load(&voice.slot));
        result.regionFrames = load(&voice.regionFrames);
        result.consumedFrames = load(&voice.consumedFrames);
        result.bufferedFrames = voice.ring.size();
        result.underruns = load(&voice.underruns);
        result.generation = load(&voice.generation);
        result.eof = load(&voice.eof) != 0;
        return result;
    }

private:
    struct Voice {
        SpscRing<int16_t, RingFrames> ring;
        alignas(4) uint32_t state;
        alignas(4) uint32_t slot;
        alignas(4) uint32_t regionFrames;
        alignas(4) uint32_t consumedFrames;
        alignas(4) uint32_t incrementQ16;
        alignas(4) uint32_t fractionQ16;
        alignas(4) uint32_t gainQ15;
        alignas(4) uint32_t cutoffQ15;
        alignas(4) uint32_t resonanceQ15;
        alignas(4) uint32_t underruns;
        alignas(4) uint32_t generation;
        alignas(4) uint32_t eof;
        float filterLow;
        float filterBand;
    };

    Voice _voices[VoiceCount];
    alignas(4) uint32_t _generation;

    static uint32_t load(const uint32_t* value) {
        return __atomic_load_n(value, __ATOMIC_ACQUIRE);
    }
    static void store(uint32_t* value, uint32_t replacement) {
        __atomic_store_n(value, replacement, __ATOMIC_RELEASE);
    }
    static uint32_t add(uint32_t* value, uint32_t increment) {
        return __atomic_add_fetch(value, increment, __ATOMIC_RELAXED);
    }
};
