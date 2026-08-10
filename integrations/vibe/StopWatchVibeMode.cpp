#include "VibeWatchMode.h"

#include "config.h"

#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

// StreetPass uses the ESP32's public BLE identity. Give the embedded
// VibeWatch HID a deterministic random-static identity so macOS never applies
// one mode's cached GATT handles to the other mode's incompatible database.
class NimBLEServer;
class NimBLEConnInfo;
bool configureEmbeddedVibeWatchBleIdentity();
bool attachStreetPassToVibeWatchBle(NimBLEServer* server);
void resetStreetPassSharedBleState();

namespace VibeWatchMode {
void onEmbeddedHidSubscribed(NimBLEConnInfo& connection);
void onEmbeddedHidConnParamsUpdated(NimBLEConnInfo& connection);
}

// Compile GOROman's reference firmware into this translation unit while
// keeping Arduino's real setup()/loop() in the parent Stack-chan firmware.
#define VIBEWATCH_CONFIGURE_BLE_IDENTITY() configureEmbeddedVibeWatchBleIdentity()
#define VIBEWATCH_BEFORE_BLE_SERVER_START(server) attachStreetPassToVibeWatchBle(server)
#define VIBEWATCH_ON_HID_SUBSCRIBED(connection) VibeWatchMode::onEmbeddedHidSubscribed(connection)
#define VIBEWATCH_ON_HID_CONN_PARAMS_UPDATED(connection) VibeWatchMode::onEmbeddedHidConnParamsUpdated(connection)
#define setup vibeWatchReferenceSetup
#define loop vibeWatchReferenceLoop
#include "generated/vibewatch/main.cpp"
#undef loop
#undef setup
#undef VIBEWATCH_ON_HID_CONN_PARAMS_UPDATED
#undef VIBEWATCH_ON_HID_SUBSCRIBED
#undef VIBEWATCH_BEFORE_BLE_SERVER_START
#undef VIBEWATCH_CONFIGURE_BLE_IDENTITY

bool configureEmbeddedVibeWatchBleIdentity() {
    const std::uint64_t chipMac = ESP.getEfuseMac();
    char addressText[18] = {};
    std::snprintf(addressText, sizeof(addressText),
                  "C2:%02X:%02X:%02X:%02X:%02X",
                  static_cast<unsigned>((chipMac >> 8) & 0xff),
                  static_cast<unsigned>((chipMac >> 16) & 0xff),
                  static_cast<unsigned>((chipMac >> 24) & 0xff),
                  static_cast<unsigned>((chipMac >> 32) & 0xff),
                  static_cast<unsigned>((chipMac >> 40) & 0xff));

    const NimBLEAddress address(addressText, BLE_ADDR_RANDOM);
    const bool addressSet = NimBLEDevice::setOwnAddr(address);
    const bool typeSet = addressSet &&
                         NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    Serial.printf("[vibewatch] BLE identity=%s static=%d ready=%d\n",
                  addressText,
                  address.isStatic() ? 1 : 0,
                  typeSet ? 1 : 0);
    return typeSet && address.isStatic();
}

namespace VibeWatchMode {
namespace {

bool embeddedActive = false;
bool embeddedUiVisible = true;
bool embeddedTransportResident = false;
volatile TransportPowerProfile embeddedTransportPowerProfile =
    TransportPowerProfile::Active;

struct ConnectionPowerParams {
    uint16_t minInterval;
    uint16_t maxInterval;
    uint16_t latency;
    uint16_t timeout;
    const char* name;
};

ConnectionPowerParams connectionPowerParams(TransportPowerProfile profile) {
    // Interval units are 1.25 ms and timeout units are 10 ms. macOS remains
    // the central and may clamp or reject these requests, so the negotiated
    // values are logged by onEmbeddedHidConnParamsUpdated().
    switch (profile) {
        case TransportPowerProfile::Background:
            return {48, 80, 4, 600, "background"};   // 60-100 ms
        case TransportPowerProfile::ScreenOff:
            return {80, 120, 8, 800, "screen_off"}; // 100-150 ms
        case TransportPowerProfile::Active:
        default:
            return {12, 24, 0, 180, "active"};       // 15-30 ms
    }
}

void requestConnectionPowerProfile(uint16_t connectionHandle) {
    if (g_server == nullptr || connectionHandle == BLE_HS_CONN_HANDLE_NONE) {
        return;
    }
    const auto profile = static_cast<TransportPowerProfile>(
        embeddedTransportPowerProfile);
    const ConnectionPowerParams params = connectionPowerParams(profile);
    g_server->updateConnParams(connectionHandle,
                               params.minInterval,
                               params.maxInterval,
                               params.latency,
                               params.timeout);
    Serial.printf("[vibewatch] HID power request profile=%s handle=%u interval=%u-%u latency=%u timeout=%u\n",
                  params.name,
                  static_cast<unsigned>(connectionHandle),
                  static_cast<unsigned>(params.minInterval),
                  static_cast<unsigned>(params.maxInterval),
                  static_cast<unsigned>(params.latency),
                  static_cast<unsigned>(params.timeout));
}


void releasePressedControls() {
    if (g_activeTouch >= 0 && g_activeTouch < kAgentCount) {
        if (g_touchActionLayer) {
            sendOuterActionEvent(g_activeTouch, false);
        } else {
            sendAgentEvent(g_activeTouch, false);
        }
    } else if (g_activeTouch == kTouchMic) {
        sendMicEvent(false);
    }

    if (g_leftAgentPressed >= 0) {
        if (g_leftPressedActionLayer) {
            sendOuterActionEvent(g_leftAgentPressed, false);
        } else {
            sendAgentEvent(g_leftAgentPressed, false);
        }
    }
    if (g_rightActionPressed) {
        sendOuterActionEvent(kOkAction, false);
    }
    if (g_rightLongTriggered) {
        sendMicEvent(false);
    }

    g_activeTouch = -1;
    g_leftAgentPressed = -1;
    g_leftPressedActionLayer = false;
    g_leftPressPending = false;
    g_rightLongTriggered = false;
    g_rightPhysicalPressedAt = 0;
    g_rightActionPending = false;
    g_rightActionPressed = false;
    g_buttonChordActive = false;
    g_vibrationOffAt = 0;
    M5.Power.setVibration(0);
    g_uiDirty = true;
}

void drainRpcQueue() {
    if (g_rpcQueue == nullptr) {
        return;
    }
    char* message = nullptr;
    while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {
        std::free(message);
        message = nullptr;
    }
}

void stopBleTransport() {
    resetStreetPassSharedBleState();
    if (g_server != nullptr) {
        // The reference callback has static storage. Ensure NimBLE does not
        // take ownership of it when deinit(true) destroys the server.
        g_server->setCallbacks(nullptr, false);
        const auto peers = g_server->getPeerDevices();
        for (const auto connectionHandle : peers) {
            g_server->disconnect(connectionHandle);
        }
    }
    if (g_vendorOutput != nullptr) {
        g_vendorOutput->setCallbacks(nullptr);
    }
    if (g_vendorInput != nullptr) {
        g_vendorInput->setCallbacks(nullptr);
    }
    if (NimBLEDevice::isInitialized()) {
        NimBLEDevice::getAdvertising()->stop();
        delay(40);
        NimBLEDevice::deinit(true);
    }
    delete g_hid;
    g_server = nullptr;
    g_hid = nullptr;
    g_vendorInput = nullptr;
    g_vendorOutput = nullptr;
    g_connected = false;
    g_hidConnectionHandle = BLE_HS_CONN_HANDLE_NONE;
    g_bleStarted = false;
    g_pairingSuccessPending = false;
    g_rxBuffer = "";
    embeddedTransportResident = false;
}

bool startBleTransport() {
    initializeBle();
    if (g_server != nullptr) {
        // Override the reference firmware's default ownership=true setting.
        g_server->setCallbacks(&g_serverCallbacks, false);
    }
    const bool ready = g_bleStarted &&
           g_server != nullptr && g_hid != nullptr &&
           g_vendorInput != nullptr && g_vendorOutput != nullptr;
    embeddedTransportResident = ready;
    return ready;
}

bool resumeVibeAdvertising() {
    if (!embeddedTransportResident || !NimBLEDevice::isInitialized() ||
        g_hid == nullptr) {
        return false;
    }
    if (g_connected) {
        return true;
    }

    auto* advertising = NimBLEDevice::getAdvertising();
    advertising->stop();
    advertising->clearData();
    advertising->setName(g_deviceName);
    advertising->setAppearance(HID_KEYBOARD);
    advertising->addServiceUUID(g_hid->getHidService()->getUUID());
    advertising->enableScanResponse(true);
    const bool started = advertising->start();
    Serial.printf("[vibewatch] resident advertising=%d name=%s\n",
                  started ? 1 : 0, g_deviceName);
    return started;
}

void restartBleTransportForPairing() {
    stopBleTransport();
    g_restartAt = 0;
    if (!startBleTransport()) {
        Serial.println("[vibewatch] BLE restart failed");
    }
    g_uiDirty = true;
}

void handleEmbeddedPhysicalButtons() {
    // The parent firmware reserves a 700 ms yellow-button hold for its mode
    // selector. Keep the reference firmware's left-button action pending while
    // yellow alone is held, then let it emit DOWN+UP on release. If blue joins
    // the gesture, dispatch immediately so the original two-button layer chord
    // remains responsive.
    const bool yellowOnlyHeld = M5.BtnA.isPressed() &&
                                !M5.BtnB.isPressed() &&
                                !M5.BtnA.wasPressed();
    if (yellowOnlyHeld && !g_buttonChordActive) {
        return;
    }
    handlePhysicalButtons();
}


}  // namespace

bool begin() {
    if (embeddedActive) {
        return true;
    }

    setTransportPowerProfile(TransportPowerProfile::Active);

    loadPreferences();
    M5.Speaker.setVolume(g_seVolume);
    updateBattery(false);
    showSplashScreen();
    initializeAgentPositions();

    if (embeddedTransportResident && g_rpcQueue != nullptr &&
        NimBLEDevice::isInitialized() && g_bleStarted) {
        if (!resumeVibeAdvertising()) {
            Serial.println("[vibewatch] resident BLE resume failed");
            return false;
        }
        embeddedActive = true;
        embeddedUiVisible = true;
        g_uiDirty = true;
        Serial.println("[vibewatch] embedded mode resumed without HID disconnect");
        return true;
    }

    drainRpcQueue();
    if (g_rpcQueue != nullptr) {
        vQueueDelete(g_rpcQueue);
    }
    g_rpcQueue = xQueueCreate(6, sizeof(char*));
    if (g_rpcQueue == nullptr) {
        Serial.println("[vibewatch] RPC queue allocation failed");
        return false;
    }

    renderUi(millis());
    if (!startBleTransport()) {
        stopBleTransport();
        vQueueDelete(g_rpcQueue);
        g_rpcQueue = nullptr;
        return false;
    }

    embeddedActive = true;
    embeddedUiVisible = true;
    g_uiDirty = true;
    Serial.println("[vibewatch] embedded mode started");
    return true;
}

void end() {
    if (!embeddedActive) {
        return;
    }

    releasePressedControls();
    // VibeWatch plays effects directly through M5.Speaker, outside the
    // parent's AudioController. M5Unified's tone() starts the speaker task and
    // I2S driver on demand, so explicitly hand the hardware back when the UI
    // is suspended. The resident BLE/HID transport does not need audio.
    if (M5.Speaker.isRunning()) {
        M5.Speaker.stop();
        M5.Speaker.end();
        Serial.println("[vibewatch] speaker/I2S stopped on mode exit");
    }
    setTransportPowerProfile(TransportPowerProfile::Background);
    embeddedActive = false;
    embeddedUiVisible = false;
    g_restartAt = 0;
    Serial.println("[vibewatch] embedded mode suspended; HID kept connected");
}

void update(uint32_t parentNow) {
    // The parent captures its loop timestamp before processing rear/display
    // touches. A touch can start the microphone or render a frame using a
    // slightly newer millis() value, so reusing the older timestamp here
    // would make unsigned elapsed-time subtraction wrap to a huge value.
    // Refresh once after input handling so the 60-second timeout and redraw
    // cadence always share the same clock ordering.
    (void)parentNow;
    const uint32_t now = millis();
    if (!embeddedTransportResident || g_rpcQueue == nullptr) {
        return;
    }

    if (embeddedActive && embeddedUiVisible) {
        handleTouch();
        handleEmbeddedPhysicalButtons();
    }

    char* message = nullptr;
    while (xQueueReceive(g_rpcQueue, &message, 0) == pdTRUE) {
        const bool savedAgentStateVibe = g_agentStateVibeEnabled;
        if (!embeddedActive) {
            // Keep Codex's RPC channel serviced in normal modes without
            // allowing background agent updates to vibrate the device.
            g_agentStateVibeEnabled = false;
        }
        processRpc(message);
        g_agentStateVibeEnabled = savedAgentStateVibe;
        std::free(message);
        message = nullptr;
    }


    if (embeddedActive && g_pairingSuccessPending) {
        g_pairingSuccessPending = false;
        playSe(1320.0f, 95);
        vibrate(220, 75);
    }

    if (embeddedActive && g_restartAt != 0 &&
        static_cast<std::int32_t>(now - g_restartAt) >= 0) {
        // The standalone firmware restarts here. Embedded mode instead
        // rebuilds only the BLE transport so the parent firmware does not
        // reboot back into Conversation mode.
        restartBleTransportForPairing();
    }
    if (g_vibrationOffAt != 0 &&
        static_cast<std::int32_t>(now - g_vibrationOffAt) >= 0) {
        M5.Power.setVibration(0);
        g_vibrationOffAt = 0;
    }
    if (now - g_lastBatteryUpdate >= kBatteryUpdatePeriodMs) {
        updateBattery(true);
    }
    const bool animateVisibleUi =
        uiIsAnimated();
    const std::uint32_t uiPeriod = g_selectionAnimating
                                       ? kSelectionAnimationPeriodMs
                                       : kUiAnimationPeriodMs;
    if (embeddedActive && embeddedUiVisible &&
        (g_uiDirty || animateVisibleUi) && now - g_lastUiDraw >= uiPeriod) {
        renderUi(now);
    }
}

void setUiVisible(bool visible) {
    if (embeddedUiVisible == visible) {
        return;
    }
    embeddedUiVisible = visible;
    if (!visible) {
        releasePressedControls();
        return;
    }
    g_uiDirty = true;
    renderUi(millis());
}

void updateBackTouch(bool detected) {
    (void)detected;
}

void pauseInputs() {
    if (embeddedActive) {
        releasePressedControls();
    }
}

void redraw() {
    if (!embeddedActive) {
        return;
    }
    g_uiDirty = true;
    renderUi(millis());
}

bool active() {
    return embeddedActive;
}

void setTransportPowerProfile(TransportPowerProfile profile) {
    const auto previous = static_cast<TransportPowerProfile>(
        embeddedTransportPowerProfile);
    embeddedTransportPowerProfile = profile;
    if (previous == profile) {
        return;
    }
    const ConnectionPowerParams params = connectionPowerParams(profile);
    Serial.printf("[vibewatch] HID power profile=%s connected=%d\n",
                  params.name, hidConnected() ? 1 : 0);
    if (hidConnected()) {
        requestConnectionPowerProfile(g_hidConnectionHandle);
    }
}

bool hidConnected() {
    return g_connected &&
           g_hidConnectionHandle != BLE_HS_CONN_HANDLE_NONE;
}

bool bleLinkConnected() {
    return g_server != nullptr && g_server->getConnectedCount() != 0;
}

void onEmbeddedHidSubscribed(NimBLEConnInfo& connection) {
    requestConnectionPowerProfile(connection.getConnHandle());
}

void onEmbeddedHidConnParamsUpdated(NimBLEConnInfo& connection) {
    if (connection.getConnHandle() != g_hidConnectionHandle) {
        return;
    }
    const ConnectionPowerParams requested = connectionPowerParams(
        static_cast<TransportPowerProfile>(embeddedTransportPowerProfile));
    Serial.printf("[vibewatch] HID power negotiated profile=%s handle=%u interval=%u(%.1fms) latency=%u timeout=%u(%ums)\n",
                  requested.name,
                  static_cast<unsigned>(connection.getConnHandle()),
                  static_cast<unsigned>(connection.getConnInterval()),
                  static_cast<double>(connection.getConnInterval()) * 1.25,
                  static_cast<unsigned>(connection.getConnLatency()),
                  static_cast<unsigned>(connection.getConnTimeout()),
                  static_cast<unsigned>(connection.getConnTimeout()) * 10U);
}

}  // namespace VibeWatchMode
