#include "boot_selector.h"

#ifndef MS16_FIRMWARE_ROLE
#define MS16_FIRMWARE_ROLE 0
#endif

static_assert(MS16_FIRMWARE_ROLE >= 0 && MS16_FIRMWARE_ROLE < BOOT_ROLE_COUNT,
              "MS16_FIRMWARE_ROLE must be 0 (normal) or 1 (USB host)");

namespace {
BootSelectorSnapshot s_snapshot = {};

BootRole compiledRole() {
    return static_cast<BootRole>(MS16_FIRMWARE_ROLE);
}
}  // namespace

#if defined(ESP32)

#include <esp_err.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_system.h>

namespace {
const esp_partition_t* partitionFor(BootRole role) {
    if (!bootRoleValid(role)) return nullptr;
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP,
                                    ESP_PARTITION_SUBTYPE_ANY,
                                    bootRolePartitionLabel(role));
}

bool validApp(const esp_partition_t* partition) {
    if (!partition) return false;
    esp_app_desc_t description = {};
    return esp_ota_get_partition_description(partition, &description) == ESP_OK;
}

void refreshSnapshot() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* configured = esp_ota_get_boot_partition();
    const esp_partition_t* normal = partitionFor(BOOT_ROLE_NORMAL);
    const esp_partition_t* usbHost = partitionFor(BOOT_ROLE_USB_HOST);
    s_snapshot.layout.compiledRole = compiledRole();
    s_snapshot.layout.runningSlotRole = running
        ? bootRoleFromPartitionLabel(running->label) : BOOT_ROLE_INVALID;
    s_snapshot.layout.normalValid = validApp(normal);
    s_snapshot.layout.usbHostValid = validApp(usbHost);
    s_snapshot.configuredBootRole = configured
        ? bootRoleFromPartitionLabel(configured->label) : BOOT_ROLE_INVALID;
    s_snapshot.layoutMatchesBuild =
        s_snapshot.layout.runningSlotRole == s_snapshot.layout.compiledRole;
}
}  // namespace

void bootSelectorInit() {
    s_snapshot = {};
    s_snapshot.configuredBootRole = BOOT_ROLE_INVALID;
    s_snapshot.lastPlatformError = ESP_OK;
    refreshSnapshot();
}

BootSwitchDecision bootSelectorPrepare(BootRole requested) {
    refreshSnapshot();
    const BootSwitchDecision decision = bootEvaluateSwitch(s_snapshot.layout, requested);
    if (decision != BOOT_SWITCH_READY) return decision;
    const esp_partition_t* target = partitionFor(requested);
    if (!target) return BOOT_SWITCH_TARGET_MISSING;
    const esp_err_t result = esp_ota_set_boot_partition(target);
    s_snapshot.lastPlatformError = result;
    if (result != ESP_OK) {
        s_snapshot.switchPending = false;
        refreshSnapshot();
        return BOOT_SWITCH_SET_FAILED;
    }
    s_snapshot.switchPending = true;
    refreshSnapshot();
    s_snapshot.switchPending = true;
    return BOOT_SWITCH_READY;
}

void bootSelectorRestart() {
    esp_restart();
}

#else

void bootSelectorInit() {
    s_snapshot = {};
    s_snapshot.layout.compiledRole = compiledRole();
    s_snapshot.layout.runningSlotRole = BOOT_ROLE_INVALID;
    s_snapshot.configuredBootRole = BOOT_ROLE_INVALID;
}

BootSwitchDecision bootSelectorPrepare(BootRole requested) {
    return bootEvaluateSwitch(s_snapshot.layout, requested);
}

void bootSelectorRestart() {}

#endif

BootSelectorSnapshot bootSelectorSnapshot() {
    return s_snapshot;
}

BootRole bootSelectorCompiledRole() {
    return compiledRole();
}
