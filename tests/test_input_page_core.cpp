#include "../input_page_core.h"
#include <assert.h>

int main() {
    assert(inputPageNeedsPerformanceStop(PAGE_CHORD));
    assert(inputPageNeedsPerformanceStop(PAGE_MEDO));
    for (uint8_t page = 0; page < PAGE_COUNT; ++page) {
        if (page == PAGE_CHORD || page == PAGE_MEDO) continue;
        assert(!inputPageNeedsPerformanceStop(static_cast<Page>(page)));
    }
    return 0;
}
