#pragma once
#include "config.h"

static inline bool inputPageNeedsPerformanceStop(Page page) {
    return page == PAGE_CHORD || page == PAGE_MEDO;
}
