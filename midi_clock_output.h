#pragma once

#include <stdint.h>

// Hardware-independent 24-PPQN scheduler. The caller emits the returned number
// of F8 bytes. Long main-loop stalls drop excess catch-up pulses instead of
// creating an unbounded burst that would monopolize the loop.
class MidiClockOutputScheduler {
public:
    void reset() { _lastUs = 0; _dropped = 0; _running = false; }
    void start(uint32_t nowUs) { _lastUs = nowUs; _running = true; }
    void stop() { _running = false; }
    bool running() const { return _running; }
    uint32_t dropped() const { return _dropped; }

    uint8_t pulsesDue(uint32_t nowUs, uint16_t bpm, uint8_t maximumBurst = 6) {
        if (!_running || bpm == 0 || maximumBurst == 0) return 0;
        const uint32_t periodUs = 60000000u / static_cast<uint32_t>(bpm) / 24u;
        if (periodUs == 0) return 0;
        const uint32_t elapsed = nowUs - _lastUs;
        const uint32_t total = elapsed / periodUs;
        if (total == 0) return 0;
        _lastUs += total * periodUs;
        if (total > maximumBurst) {
            _dropped += total - maximumBurst;
            return maximumBurst;
        }
        return static_cast<uint8_t>(total);
    }

private:
    uint32_t _lastUs = 0;
    uint32_t _dropped = 0;
    bool _running = false;
};
