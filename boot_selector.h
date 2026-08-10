#pragma once

#include "boot_selector_core.h"

#include <stdint.h>

struct BootSelectorSnapshot {
    BootLayoutState layout;
    BootRole configuredBootRole;
    bool layoutMatchesBuild;
    bool switchPending;
    int32_t lastPlatformError;
};

void bootSelectorInit();
BootSelectorSnapshot bootSelectorSnapshot();
BootRole bootSelectorCompiledRole();
BootSwitchDecision bootSelectorPrepare(BootRole requested);
void bootSelectorRestart();
