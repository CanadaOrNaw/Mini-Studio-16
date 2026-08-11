#include "../audio_cap_bridge_core.h"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

static void testHostDeviceRoundTrip() {
    AudioCapHostCore host;
    AudioCapDeviceCore device;
    int16_t source[256];
    for (int i = 0; i < 256; ++i) source[i] = static_cast<int16_t>(i * 100 - 12000);
    assert(host.pushPlayback(source, 256) == 256);

    AudioCapPacket outbound;
    host.buildTransfer(outbound, AUDIO_CAP_CMD_PAIR, 73);
    assert(device.acceptTransfer(outbound));
    assert(device.monitorPercent() == 73);
    assert(device.takeCommands() == AUDIO_CAP_CMD_PAIR);
    assert(device.takeCommands() == 0);
    int16_t playback[128] = {};
    assert(device.popPlayback(playback, 128) == 128);
    assert(std::memcmp(source, playback, sizeof(playback)) == 0);

    assert(device.pushCapture(playback, 128) == 128);
    AudioCapPacket reply;
    device.buildReply(reply, AUDIO_CAP_STATUS_PRESENT | AUDIO_CAP_STATUS_ADC_LOCKED);
    assert(host.acceptReply(reply));
    int16_t capture[128] = {};
    assert(host.popCapture(capture, 128) == 128);
    assert(std::memcmp(playback, capture, sizeof(capture)) == 0);
    assert(host.remoteStatus() & AUDIO_CAP_STATUS_PRESENT);

    reply.crc32 ^= 1;
    assert(!host.acceptReply(reply));
    assert(host.stats().packetErrors == 1);
}

static std::vector<int16_t> upsample(const std::vector<int16_t>& input,
                                     const std::vector<size_t>& chunks) {
    AudioCapPlaybackUpsampler converter;
    std::vector<int16_t> output(input.size() * 4);
    size_t inOffset = 0, outFrames = 0;
    for (size_t chunk : chunks) {
        outFrames += converter.process(input.data() + inOffset, chunk,
                                       output.data() + outFrames * 2,
                                       input.size() * 2 - outFrames);
        inOffset += chunk;
    }
    output.resize(outFrames * 2);
    return output;
}

static void testPlaybackConversion() {
    std::vector<int16_t> input(257);
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<int16_t>(i * 91 - 9000);
    const auto one = upsample(input, {input.size()});
    const auto chunks = upsample(input, {1, 31, 17, 128, 80});
    assert(one == chunks);
    assert(one.size() == input.size() * 4);
    for (size_t frame = 0; frame < input.size() * 2; ++frame)
        assert(one[frame * 2] == one[frame * 2 + 1]);
}

static std::vector<int16_t> resample(const std::vector<int32_t>& input,
                                     const std::vector<size_t>& chunks) {
    AudioCapCaptureResampler converter;
    std::vector<int16_t> output(input.size());
    size_t inFrames = 0, written = 0;
    for (size_t chunk : chunks) {
        written += converter.process(input.data() + inFrames * 2, chunk,
                                     output.data() + written, output.size() - written);
        inFrames += chunk;
    }
    output.resize(written);
    return output;
}

static void testCaptureConversion() {
    constexpr size_t frames = 4800;
    std::vector<int32_t> input(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const int16_t sample = static_cast<int16_t>(12000.0 *
            std::sin(2.0 * 3.141592653589793 * 1000.0 * i / 48000.0));
        input[i * 2] = static_cast<int32_t>(sample) * 65536;
        input[i * 2 + 1] = static_cast<int32_t>(sample) * 65536;
    }
    const auto one = resample(input, {frames});
    const auto chunks = resample(input, {17, 63, 1024, 1, 3695});
    assert(one == chunks);
    assert(one.size() == 2205);
    int16_t peak = 0;
    for (int16_t sample : one) peak = std::max<int16_t>(peak, std::abs(sample));
    assert(peak > 5000 && peak <= 32767);

    std::vector<int32_t> high(frames * 2);
    for (size_t i = 0; i < frames; ++i) {
        const int16_t sample = static_cast<int16_t>((i & 1) ? 16000 : -16000);
        high[i * 2] = high[i * 2 + 1] = static_cast<int32_t>(sample) * 65536;
    }
    const auto filtered = resample(high, {frames});
    int64_t meanAbs = 0;
    for (size_t i = 100; i < filtered.size(); ++i) meanAbs += std::abs(filtered[i]);
    meanAbs /= static_cast<int64_t>(filtered.size() - 100);
    assert(meanAbs < 1000);
}

static void testOverflowAndSequenceWrap() {
    AudioCapHostCore host;
    std::vector<int16_t> tooMuch(3000, 7);
    assert(host.pushPlayback(tooMuch.data(), tooMuch.size()) == 2048);
    assert(host.stats().playbackDrops == 952);

    AudioCapDeviceCore device;
    AudioCapPacket first, second;
    audioCapPacketInit(first, 0xFFFFFFFFu, 0, nullptr, 0);
    audioCapPacketInit(second, 0u, 0, nullptr, 0);
    assert(device.acceptTransfer(first));
    assert(device.acceptTransfer(second));
    assert(device.stats().sequenceGaps == 0);
}

int main() {
    testHostDeviceRoundTrip();
    testPlaybackConversion();
    testCaptureConversion();
    testOverflowAndSequenceWrap();
    std::cout << "audio_cap_bridge_core: transport and conversion passed\n";
}
