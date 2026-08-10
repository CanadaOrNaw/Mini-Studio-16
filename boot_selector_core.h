#pragma once

#include <stdint.h>

enum BootRole : uint8_t {
    BOOT_ROLE_NORMAL = 0,
    BOOT_ROLE_USB_HOST = 1,
    BOOT_ROLE_COUNT = 2,
    BOOT_ROLE_INVALID = 0xff,
};

enum BootSwitchDecision : uint8_t {
    BOOT_SWITCH_READY = 0,
    BOOT_SWITCH_ALREADY_ACTIVE,
    BOOT_SWITCH_INVALID_ROLE,
    BOOT_SWITCH_TARGET_MISSING,
    BOOT_SWITCH_LAYOUT_MISMATCH,
    BOOT_SWITCH_SET_FAILED,
};

enum BootRuntimeBlocker : uint8_t {
    BOOT_RUNTIME_READY = 0,
    BOOT_RUNTIME_RECORDING_BUSY,
    BOOT_RUNTIME_STORAGE_BUSY,
    BOOT_RUNTIME_DIAGNOSTIC_BUSY,
};

struct BootLayoutState {
    BootRole compiledRole;
    BootRole runningSlotRole;
    bool normalValid;
    bool usbHostValid;
};

struct BootRuntimeActivity {
    bool masterRecording;
    bool stemRecording;
    bool microphoneRecording;
    bool loopRecording;
    bool sampleRecording;
    bool microphoneCommitPending;
    bool loopClearPending;
    bool sampleMutationPending;
    bool sdDiagnosticRunning;
};

const char* bootRoleName(BootRole role);
const char* bootRolePartitionLabel(BootRole role);
BootRole bootRoleFromPartitionLabel(const char* label);
BootRole bootOtherRole(BootRole role);
bool bootRoleValid(BootRole role);
BootSwitchDecision bootEvaluateSwitch(const BootLayoutState& state,
                                      BootRole requested);
const char* bootSwitchDecisionName(BootSwitchDecision decision);
BootRuntimeBlocker bootEvaluateRuntime(const BootRuntimeActivity& activity);
const char* bootRuntimeBlockerName(BootRuntimeBlocker blocker);
