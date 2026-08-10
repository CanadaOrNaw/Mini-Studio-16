#include "../boot_selector_core.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    assert(bootRoleValid(BOOT_ROLE_NORMAL));
    assert(bootRoleValid(BOOT_ROLE_USB_HOST));
    assert(!bootRoleValid(BOOT_ROLE_INVALID));
    assert(bootOtherRole(BOOT_ROLE_NORMAL) == BOOT_ROLE_USB_HOST);
    assert(bootOtherRole(BOOT_ROLE_USB_HOST) == BOOT_ROLE_NORMAL);
    assert(bootOtherRole(BOOT_ROLE_INVALID) == BOOT_ROLE_INVALID);
    assert(bootRoleFromPartitionLabel("normal") == BOOT_ROLE_NORMAL);
    assert(bootRoleFromPartitionLabel("usbhost") == BOOT_ROLE_USB_HOST);
    assert(bootRoleFromPartitionLabel("factory") == BOOT_ROLE_INVALID);
    assert(bootRoleFromPartitionLabel(nullptr) == BOOT_ROLE_INVALID);
    assert(std::strcmp(bootRolePartitionLabel(BOOT_ROLE_USB_HOST), "usbhost") == 0);

    BootLayoutState state = {
        BOOT_ROLE_NORMAL, BOOT_ROLE_NORMAL, true, true,
    };
    assert(bootEvaluateSwitch(state, BOOT_ROLE_USB_HOST) == BOOT_SWITCH_READY);
    assert(bootEvaluateSwitch(state, BOOT_ROLE_NORMAL) ==
           BOOT_SWITCH_ALREADY_ACTIVE);
    assert(bootEvaluateSwitch(state, BOOT_ROLE_INVALID) ==
           BOOT_SWITCH_INVALID_ROLE);

    state.usbHostValid = false;
    assert(bootEvaluateSwitch(state, BOOT_ROLE_USB_HOST) ==
           BOOT_SWITCH_TARGET_MISSING);

    state.usbHostValid = true;
    state.runningSlotRole = BOOT_ROLE_USB_HOST;
    assert(bootEvaluateSwitch(state, BOOT_ROLE_USB_HOST) ==
           BOOT_SWITCH_LAYOUT_MISMATCH);

    state.compiledRole = BOOT_ROLE_INVALID;
    assert(bootEvaluateSwitch(state, BOOT_ROLE_NORMAL) ==
           BOOT_SWITCH_INVALID_ROLE);

    state = {BOOT_ROLE_USB_HOST, BOOT_ROLE_USB_HOST, true, true};
    assert(bootEvaluateSwitch(state, BOOT_ROLE_NORMAL) == BOOT_SWITCH_READY);
    assert(std::strcmp(bootSwitchDecisionName(BOOT_SWITCH_SET_FAILED),
                       "set_failed") == 0);

    BootRuntimeActivity activity = {};
    assert(bootEvaluateRuntime(activity) == BOOT_RUNTIME_READY);
    assert(std::strcmp(bootRuntimeBlockerName(BOOT_RUNTIME_READY), "ready") == 0);

    bool* recordingFlags[] = {
        &activity.masterRecording, &activity.stemRecording,
        &activity.microphoneRecording, &activity.loopRecording,
        &activity.sampleRecording,
    };
    for (bool* flag : recordingFlags) {
        *flag = true;
        assert(bootEvaluateRuntime(activity) == BOOT_RUNTIME_RECORDING_BUSY);
        *flag = false;
    }

    bool* mutationFlags[] = {
        &activity.microphoneCommitPending, &activity.loopClearPending,
        &activity.sampleMutationPending,
    };
    for (bool* flag : mutationFlags) {
        *flag = true;
        assert(bootEvaluateRuntime(activity) == BOOT_RUNTIME_STORAGE_BUSY);
        *flag = false;
    }

    activity.sdDiagnosticRunning = true;
    assert(bootEvaluateRuntime(activity) == BOOT_RUNTIME_DIAGNOSTIC_BUSY);
    activity.masterRecording = true;
    assert(bootEvaluateRuntime(activity) == BOOT_RUNTIME_RECORDING_BUSY);
    assert(std::strcmp(bootRuntimeBlockerName(BOOT_RUNTIME_RECORDING_BUSY),
                       "boot_recording_busy") == 0);
    assert(std::strcmp(bootRuntimeBlockerName(BOOT_RUNTIME_STORAGE_BUSY),
                       "boot_storage_busy") == 0);
    assert(std::strcmp(bootRuntimeBlockerName(BOOT_RUNTIME_DIAGNOSTIC_BUSY),
                       "boot_diagnostic_busy") == 0);

    std::cout << "boot_selector_core: roles, recovery, runtime safety and bounds passed\n";
    return 0;
}
