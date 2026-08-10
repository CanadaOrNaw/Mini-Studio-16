#include "../sample_reference.h"

#include <cassert>
#include <iostream>

int main() {
    for (uint8_t slot = 0; slot < SAMPLER_SLOT_COUNT; ++slot) {
        const int8_t reference = sampleEncodeStreamReference(slot);
        assert(reference <= -2);
        uint8_t decoded = 0xFF;
        assert(sampleDecodeStreamReference(reference, decoded));
        assert(decoded == slot);
    }
    assert(sampleEncodeStreamReference(SAMPLER_SLOT_COUNT) == -1);
    uint8_t decoded = 0xFF;
    assert(!sampleDecodeStreamReference(-1, decoded));
    assert(!sampleDecodeStreamReference(0, decoded));
    assert(!sampleDecodeStreamReference(-18, decoded));
    std::cout << "sample_reference: all 16 streamed sentinels passed\n";
    return 0;
}
