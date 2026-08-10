#include "boot_selector_core.h"

#include <string.h>

const char* bootRoleName(BootRole role) {
    switch (role) {
        case BOOT_ROLE_NORMAL: return "normal";
        case BOOT_ROLE_USB_HOST: return "usb_host";
        default: return "invalid";
    }
}

const char* bootRolePartitionLabel(BootRole role) {
    switch (role) {
        case BOOT_ROLE_NORMAL: return "normal";
        case BOOT_ROLE_USB_HOST: return "usbhost";
        default: return "";
    }
}

BootRole bootRoleFromPartitionLabel(const char* label) {
    if (!label) return BOOT_ROLE_INVALID;
    if (strcmp(label, "normal") == 0) return BOOT_ROLE_NORMAL;
    if (strcmp(label, "usbhost") == 0) return BOOT_ROLE_USB_HOST;
    return BOOT_ROLE_INVALID;
}

BootRole bootOtherRole(BootRole role) {
    if (role == BOOT_ROLE_NORMAL) return BOOT_ROLE_USB_HOST;
    if (role == BOOT_ROLE_USB_HOST) return BOOT_ROLE_NORMAL;
    return BOOT_ROLE_INVALID;
}

bool bootRoleValid(BootRole role) {
    return role == BOOT_ROLE_NORMAL || role == BOOT_ROLE_USB_HOST;
}

BootSwitchDecision bootEvaluateSwitch(const BootLayoutState& state,
                                      BootRole requested) {
    if (!bootRoleValid(state.compiledRole) || !bootRoleValid(requested))
        return BOOT_SWITCH_INVALID_ROLE;
    if (state.runningSlotRole != state.compiledRole)
        return BOOT_SWITCH_LAYOUT_MISMATCH;
    if (requested == state.compiledRole)
        return BOOT_SWITCH_ALREADY_ACTIVE;
    const bool valid = requested == BOOT_ROLE_NORMAL
        ? state.normalValid : state.usbHostValid;
    return valid ? BOOT_SWITCH_READY : BOOT_SWITCH_TARGET_MISSING;
}

const char* bootSwitchDecisionName(BootSwitchDecision decision) {
    switch (decision) {
        case BOOT_SWITCH_READY: return "ready";
        case BOOT_SWITCH_ALREADY_ACTIVE: return "already_active";
        case BOOT_SWITCH_INVALID_ROLE: return "invalid_role";
        case BOOT_SWITCH_TARGET_MISSING: return "target_missing";
        case BOOT_SWITCH_LAYOUT_MISMATCH: return "layout_mismatch";
        case BOOT_SWITCH_SET_FAILED: return "set_failed";
        default: return "switch_error";
    }
}
