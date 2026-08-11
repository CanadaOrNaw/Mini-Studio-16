#include "usb_midi.h"

#include "midi_input.h"

#if defined(MS16_USB_MIDI_HOST)

#include "pcm_ring.h"
#include "usb_midi_host_descriptor.h"

#include <Arduino.h>
#include <esp_intr_alloc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <usb/usb_host.h>

namespace {
constexpr uint32_t kActionOpen = 1u << 0;
constexpr uint32_t kActionClose = 1u << 1;

struct ReceivedMidiMessage {
    uint8_t length;
    uint8_t bytes[3];
};

SpscRing<ReceivedMidiMessage, 64> s_messages;
usb_host_client_handle_t s_client = nullptr;
usb_device_handle_t s_device = nullptr;
usb_transfer_t* s_inputTransfer = nullptr;
UsbMidiHostInterface s_interface = {};
alignas(4) uint32_t s_actions = 0;
alignas(4) uint32_t s_deviceAddress = 0;
alignas(4) uint32_t s_available = 0;
alignas(4) uint32_t s_mounted = 0;
alignas(4) uint32_t s_bytesReceived = 0;
alignas(4) uint32_t s_messagesSent = 0;
alignas(4) uint32_t s_errors = 0;
bool s_transferInFlight = false;
bool s_cancelRequested = false;
bool s_interfaceClaimed = false;
// P2-2 (reconciliation report): transient IN-transfer errors (STALL/
// ERROR/TIMEOUT/OVERFLOW) used to end resubmission permanently — one bus
// glitch silently killed controller input until replug. Errors now clear
// the endpoint halt and resubmit with a bounded budget; the budget refills
// on every completed transfer so only a persistently failing endpoint
// stops (and that stop is visible in the error counter).
constexpr uint32_t kTransferRetryBudget = 8;
uint32_t s_transferRetriesLeft = kTransferRetryBudget;

void inputTransferComplete(usb_transfer_t* transfer) {
    s_transferInFlight = false;
    bool resubmit = false;
    if (transfer->status == USB_TRANSFER_STATUS_COMPLETED) {
        for (int offset = 0; offset + 3 < transfer->actual_num_bytes; offset += 4) {
            const uint8_t length = usbMidiEventPacketLength(
                transfer->data_buffer[offset] & 0x0Fu);
            if (!length) continue;
            ReceivedMidiMessage message = {};
            message.length = length;
            for (uint8_t index = 0; index < length; ++index)
                message.bytes[index] = transfer->data_buffer[offset + 1 + index];
            if (!s_messages.pushOne(message)) {
                __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
            } else {
                __atomic_add_fetch(&s_bytesReceived, length, __ATOMIC_RELAXED);
            }
        }
        s_transferRetriesLeft = kTransferRetryBudget;  // healthy again (P2-2)
        resubmit = true;
    } else if (transfer->status != USB_TRANSFER_STATUS_CANCELED &&
               transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        // P2-2: recover from transient endpoint errors instead of silently
        // stopping input forever. Clearing the halt is required after a
        // STALL and harmless otherwise; a bounded budget prevents an
        // unplugged-mid-transfer endpoint from spinning.
        if (s_device && s_transferRetriesLeft > 0) {
            --s_transferRetriesLeft;
            usb_host_endpoint_clear(s_device, transfer->bEndpointAddress);
            resubmit = true;
        }
    }

    if (resubmit && __atomic_load_n(&s_mounted, __ATOMIC_ACQUIRE) != 0) {
        transfer->num_bytes = static_cast<int>(transfer->data_buffer_size);
        if (usb_host_transfer_submit(transfer) == ESP_OK) s_transferInFlight = true;
        else __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
    }
}

void closeDevice() {
    __atomic_store_n(&s_mounted, 0u, __ATOMIC_RELEASE);
    if (s_inputTransfer && s_transferInFlight) {
        if (!s_cancelRequested) {
            if (s_device) {
                usb_host_endpoint_halt(s_device, s_interface.inputEndpoint);
                usb_host_endpoint_flush(s_device, s_interface.inputEndpoint);
            }
            s_cancelRequested = true;
        }
        return;
    }
    if (s_inputTransfer) {
        usb_host_transfer_free(s_inputTransfer);
        s_inputTransfer = nullptr;
    }
    if (s_device && s_interfaceClaimed)
        usb_host_interface_release(s_client, s_device, s_interface.interfaceNumber);
    if (s_device) usb_host_device_close(s_client, s_device);
    s_device = nullptr;
    s_interface = {};
    s_messages.reset();
    s_transferInFlight = false;
    s_cancelRequested = false;
    s_interfaceClaimed = false;
    __atomic_store_n(&s_deviceAddress, 0u, __ATOMIC_RELEASE);
    __atomic_fetch_and(&s_actions, ~kActionClose, __ATOMIC_ACQ_REL);
}

void openDevice() {
    const uint8_t address = static_cast<uint8_t>(
        __atomic_load_n(&s_deviceAddress, __ATOMIC_ACQUIRE));
    __atomic_fetch_and(&s_actions, ~kActionOpen, __ATOMIC_ACQ_REL);
    if (!address || usb_host_device_open(s_client, address, &s_device) != ESP_OK) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        return;
    }
    const usb_config_desc_t* config = nullptr;
    if (usb_host_get_active_config_descriptor(s_device, &config) != ESP_OK || !config) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_or(&s_actions, kActionClose, __ATOMIC_ACQ_REL);
        return;
    }
    s_interface = usbMidiFindStreamingInterface(
        reinterpret_cast<const uint8_t*>(config), config->wTotalLength);
    if (!s_interface.found ||
        usb_host_interface_claim(s_client, s_device, s_interface.interfaceNumber,
                                 s_interface.alternateSetting) != ESP_OK) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_or(&s_actions, kActionClose, __ATOMIC_ACQ_REL);
        return;
    }
    s_interfaceClaimed = true;
    if (usb_host_transfer_alloc(s_interface.inputMaxPacket, 0,
                                &s_inputTransfer) != ESP_OK) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_or(&s_actions, kActionClose, __ATOMIC_ACQ_REL);
        return;
    }
    s_inputTransfer->device_handle = s_device;
    s_inputTransfer->bEndpointAddress = s_interface.inputEndpoint;
    s_inputTransfer->num_bytes = s_interface.inputMaxPacket;
    s_inputTransfer->callback = inputTransferComplete;
    s_inputTransfer->context = nullptr;
    __atomic_store_n(&s_mounted, 1u, __ATOMIC_RELEASE);
    if (usb_host_transfer_submit(s_inputTransfer) == ESP_OK) {
        s_transferInFlight = true;
    } else {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        __atomic_fetch_or(&s_actions, kActionClose, __ATOMIC_ACQ_REL);
    }
}

void clientEvent(const usb_host_client_event_msg_t* event, void*) {
    if (event->event == USB_HOST_CLIENT_EVENT_NEW_DEV) {
        if (__atomic_load_n(&s_deviceAddress, __ATOMIC_ACQUIRE) == 0) {
            __atomic_store_n(&s_deviceAddress, event->new_dev.address, __ATOMIC_RELEASE);
            __atomic_fetch_or(&s_actions, kActionOpen, __ATOMIC_ACQ_REL);
        }
    } else if (event->event == USB_HOST_CLIENT_EVENT_DEV_GONE) {
        __atomic_store_n(&s_mounted, 0u, __ATOMIC_RELEASE);
        __atomic_fetch_or(&s_actions, kActionClose, __ATOMIC_ACQ_REL);
    }
}

void daemonTask(void*) {
    while (true) {
        uint32_t flags = 0;
        if (usb_host_lib_handle_events(portMAX_DELAY, &flags) != ESP_OK)
            __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
    }
}

void clientTask(void*) {
    usb_host_client_config_t config = {};
    config.is_synchronous = false;
    config.max_num_event_msg = 5;
    config.async.client_event_callback = clientEvent;
    config.async.callback_arg = nullptr;
    if (usb_host_client_register(&config, &s_client) != ESP_OK) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        vTaskDelete(nullptr);
        return;
    }
    __atomic_store_n(&s_available, 1u, __ATOMIC_RELEASE);
    while (true) {
        usb_host_client_handle_events(s_client, pdMS_TO_TICKS(10));
        const uint32_t actions = __atomic_load_n(&s_actions, __ATOMIC_ACQUIRE);
        if (actions & kActionClose) closeDevice();
        else if (actions & kActionOpen) openDevice();
    }
}
}  // namespace

void usbMidiInit() {
    s_messages.reset();
    __atomic_store_n(&s_actions, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_deviceAddress, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_available, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_mounted, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_bytesReceived, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_messagesSent, 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_errors, 0u, __ATOMIC_RELEASE);
    usb_host_config_t config = {};
    config.skip_phy_setup = false;
    config.intr_flags = ESP_INTR_FLAG_LEVEL1;
    if (usb_host_install(&config) != ESP_OK) {
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
        return;
    }
    TaskHandle_t daemon = nullptr;
    TaskHandle_t client = nullptr;
    if (xTaskCreatePinnedToCore(daemonTask, "usb_host", 4096, nullptr, 2,
                                &daemon, 1) != pdPASS ||
        xTaskCreatePinnedToCore(clientTask, "usb_midi_host", 6144, nullptr, 2,
                                &client, 1) != pdPASS)
        __atomic_add_fetch(&s_errors, 1u, __ATOMIC_RELAXED);
}

void usbMidiUpdate() {
    ReceivedMidiMessage message = {};
    uint8_t budget = 32;
    while (budget-- && s_messages.popOne(message))
        for (uint8_t index = 0; index < message.length; ++index)
            midiInputFeedByte(message.bytes[index]);
}

bool usbMidiSend(const uint8_t*, size_t) {
    // The first host profile is intentionally controller-input-only. Internal
    // note/CC mirrors are therefore unsupported, not transport errors.
    return false;
}

UsbMidiSnapshot usbMidiSnapshot() {
    UsbMidiSnapshot result = {};
    result.available = __atomic_load_n(&s_available, __ATOMIC_ACQUIRE) != 0;
    result.mounted = __atomic_load_n(&s_mounted, __ATOMIC_ACQUIRE) != 0;
    result.hostMode = true;
    result.bytesReceived = __atomic_load_n(&s_bytesReceived, __ATOMIC_RELAXED);
    result.messagesSent = __atomic_load_n(&s_messagesSent, __ATOMIC_RELAXED);
    result.sendErrors = __atomic_load_n(&s_errors, __ATOMIC_RELAXED);
    return result;
}

#else

#include <Adafruit_TinyUSB.h>

namespace {
Adafruit_USBD_MIDI s_usbMidi;
UsbMidiSnapshot s_snapshot = {};
}  // namespace

void usbMidiInit() {
    s_snapshot = {};
    s_snapshot.hostMode = false;
    s_snapshot.available = s_usbMidi.begin();
}

void usbMidiUpdate() {
    if (!s_snapshot.available) return;
    s_snapshot.mounted = TinyUSBDevice.mounted();
    uint8_t budget = 64;
    while (budget-- && s_usbMidi.available() > 0) {
        const int value = s_usbMidi.read();
        if (value < 0) break;
        midiInputFeedByte(static_cast<uint8_t>(value));
        ++s_snapshot.bytesReceived;
    }
}

bool usbMidiSend(const uint8_t* message, size_t length) {
    if (!s_snapshot.available || !message || length == 0 || length > 3) return false;
    const bool ok = s_usbMidi.write(message, length) == length;
    if (ok) ++s_snapshot.messagesSent;
    else ++s_snapshot.sendErrors;
    return ok;
}

UsbMidiSnapshot usbMidiSnapshot() { return s_snapshot; }

#endif
